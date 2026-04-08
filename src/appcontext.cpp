/*
 * iDescriptor: A free and open-source idevice management tool.
 *
 * Copyright (C) 2025 Uncore <https://github.com/uncor3>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Affero General Public License as published
 * by the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Affero General Public License for more details.
 *
 * You should have received a copy of the GNU Affero General Public License
 * along with this program. If not, see <https://www.gnu.org/licenses/>.
 */

#include "appcontext.h"

AppContext *AppContext::sharedInstance()
{
    static AppContext instance;
    return &instance;
}

/*
 FIXME: waking up from sleep disconnects all devices
 and does not reconnect them until the user plugs them
 back in, even if they are still connected
*/
AppContext::AppContext(QObject *parent) : QObject{parent}
{
    cachePairedDevices();
}

void AppContext::cachePairedDevices()
{

#ifndef __APPLE__
    m_pairingFileCache = core->get_pairing_files();
#else
    QMap<QString, QString> maybeStalePairingFiles =
        SettingsManager::sharedInstance()->getAllIdeviceDefaultPairingFiles();

    for (const QString &mac : maybeStalePairingFiles.keys()) {
        const QString path = maybeStalePairingFiles.value(mac);
        qDebug() << "Using pairing file" << path << "for MAC:" << mac
                 << "cached from settings";
        m_pairingFileCache[mac] = QVariant(path);
    }
    QMap<QString, QVariant> fresh = core->get_pairing_files();
    for (const QString &mac : fresh.keys()) {
        const QString path = fresh.value(mac).toString();
        qDebug() << "Using fresh pairing file" << path << "for MAC:" << mac
                 << "from backend";
        m_pairingFileCache[mac] = QVariant(path);
    }

#endif
}

/* addDevice is only called with udid from backend */
void AppContext::addDevice(iDescriptor::Uniq uniq,
                           iDescriptor::IdeviceConnectionType conn_type,
                           AddType addType, QString info,
                           QString wifiMacAddress, QString ipAddress)
{

    if (QCoreApplication::closingDown()) {
        qDebug() << "Ignoring addDevice during shutdown for" << uniq.get();
        return;
    }

    std::shared_ptr<iDescriptorDevice> existingDevice = nullptr;
    existingDevice = getDevice(uniq.get());

    if (existingDevice) {
        uniq.isMac() ? emit deviceAlreadyExistsMAC(uniq)
                     : emit deviceAlreadyExists(uniq);
        // TODO: add a setting for this

        setCurrentDeviceSelection(DeviceSelection(existingDevice->udid), true);
        return;
    }

    if (addType == AddType::Pairing) {
        handlePairing(uniq, true);
        return;
    }

    if (addType == AddType::FailedToPair) {
        // FIXME: no widget is listening for this signal for now
        // emit pairingFailed(uniq);
        return;
    }

    qDebug() << "Device initialized: " << uniq;

    if (m_pendingDevices.contains(uniq)) {
        qDebug() << "Removing from pending devices: " << uniq;
        m_pendingDevices.removeAll(uniq);
        emit devicePairingExpired(uniq);
    }

    pugi::xml_document doc;
    auto res = doc.load_string(info.toUtf8().constData());
    if (!res) {
        core->remove_device(uniq);
        QMessageBox::warning(nullptr, "Failed to add device",
                             "Failed to parse device info XML for device " +
                                 uniq.get() +
                                 ". The device may not function "
                                 "correctly. Please report this issue.");
        qWarning() << "Failed to parse device info XML for" << uniq << ":"
                   << res.description();
        return;
    }

    DeviceInfo deviceInfo;
    fullDeviceInfo(doc, deviceInfo);

    const iDescriptorDevice device = {
        .udid = uniq.get(),
        .conn_type = conn_type,
        .deviceInfo = deviceInfo,
        .ios_version = deviceInfo.parsedDeviceVersion.major,
        .service_manager = new CXX::ServiceManager(
            uniq.get(), deviceInfo.parsedDeviceVersion.major),
        .afc_backend = new CXX::AfcBackend(uniq.get()),
        .afc2_backend = new CXX::Afc2Backend(uniq.get())};

    m_devices[device.udid] = std::make_shared<iDescriptorDevice>(device);

    if (addType == AddType::Wireless || addType == AddType::UpgradeToWireless ||
        addType == AddType::Regular) {
        qDebug() << "Wireless device added: " << uniq;

        emit deviceAdded(m_devices[device.udid]);
        emit deviceChange();
        return;
    }
    emit devicePaired(m_devices[device.udid]);
    emit deviceChange();
    m_pendingDevices.removeAll(uniq);
}

int AppContext::getConnectedDeviceCount() const
{
#ifdef ENABLE_RECOVERY_DEVICE_SUPPORT
    return m_devices.size() + m_recoveryDevices.size();
#else
    return m_devices.size();
#endif
}

void AppContext::removeDevice(iDescriptor::Uniq uniq, bool ask_backend)
{
    qDebug() << "AppContext::removeDevice device with"
             << (uniq.isMac() ? "MAC" : "UDID") << uniq.get();

    QString q_udid = uniq.get();

    if (m_pendingDevices.contains(q_udid)) {
        m_pendingDevices.removeAll(q_udid);
        emit devicePairingExpired(q_udid);
        emit deviceChange();
        return;
    } else {
        qDebug() << "Device with UUID " + q_udid +
                        " not found in pending devices.";
    }

    if (!m_devices.contains(q_udid)) {
        qDebug() << "Device with UUID " + q_udid +
                        " not found in normal devices.";
        return;
    }

    auto device = m_devices[q_udid];
    m_devices.remove(q_udid);

    qDebug() << "Removed device with UUID " + q_udid << "macAddress:"
             << QString::fromStdString(device->deviceInfo.wifiMacAddress)
             << "ipAddress:"
             << QString::fromStdString(device->deviceInfo.ipAddress)
             << "wasWireless:" << device->deviceInfo.isWireless;

    emit deviceRemoved(q_udid, device->deviceInfo.wifiMacAddress,
                       device->deviceInfo.ipAddress,
                       device->deviceInfo.isWireless);
    emit deviceChange();

    if (ask_backend) {
        core->remove_device(q_udid);
    }
}

#ifdef ENABLE_RECOVERY_DEVICE_SUPPORT
void AppContext::removeRecoveryDevice(uint64_t ecid)
{
    if (!m_recoveryDevices.contains(ecid)) {
        qDebug() << "Device with ECID " + QString::number(ecid) +
                        " not found. Please report this issue.";
        return;
    }

    qDebug() << "Removing recovery device with ECID:" << ecid;

    iDescriptorRecoveryDevice *deviceInfo = m_recoveryDevices.value(ecid);
    m_recoveryDevices.remove(ecid);

    emit recoveryDeviceRemoved(ecid);
    // TODO: do we need this ?
    // emit deviceChange();

    std::lock_guard<std::recursive_mutex> lock(deviceInfo->mutex);
    delete deviceInfo;
}
#endif

std::shared_ptr<iDescriptorDevice> AppContext::getDevice(const QString &uniq)
{
    auto it = m_devices.find(uniq);
    if (it != m_devices.end()) {
        return it.value();
    }
    return nullptr;
}

QList<std::shared_ptr<iDescriptorDevice>> AppContext::getAllDevices()
{
    return m_devices.values();
}

#ifdef ENABLE_RECOVERY_DEVICE_SUPPORT
QList<iDescriptorRecoveryDevice *> AppContext::getAllRecoveryDevices()
{
    return m_recoveryDevices.values();
}
#endif

// Returns whether there are any devices connected (regular or recovery)
bool AppContext::noDevicesConnected() const
{
#ifdef ENABLE_RECOVERY_DEVICE_SUPPORT
    return (m_devices.isEmpty() && m_recoveryDevices.isEmpty() &&
            m_pendingDevices.isEmpty());
#else
    return (m_devices.isEmpty() && m_pendingDevices.isEmpty());
#endif
}

#ifdef ENABLE_RECOVERY_DEVICE_SUPPORT
void AppContext::addRecoveryDevice(uint64_t ecid)
{
    auto res = std::make_shared<iDescriptorInitDeviceResultRecovery>();

    QFuture<void> future = QtConcurrent::run(
        [this, ecid, res]() { init_idescriptor_recovery_device(ecid, *res); });
    QFutureWatcher<void> *watcher = new QFutureWatcher<void>();
    watcher->setFuture(future);
    connect(watcher, &QFutureWatcher<void>::finished, this,
            [this, ecid, res, watcher]() {
                watcher->deleteLater();
                if (!res->success) {
                    qDebug()
                        << "Failed to initialize recovery device with ECID: "
                        << QString::number(ecid);
                    qDebug() << "Error code: " << res->error;
                    return;
                }

                iDescriptorRecoveryDevice *recoveryDevice =
                    new iDescriptorRecoveryDevice();
                recoveryDevice->ecid = res->deviceInfo.ecid;
                recoveryDevice->mode = res->mode;
                recoveryDevice->cpid = res->deviceInfo.cpid;
                recoveryDevice->bdid = res->deviceInfo.bdid;
                recoveryDevice->displayName = res->displayName;

                m_recoveryDevices[res->deviceInfo.ecid] = recoveryDevice;
                emit recoveryDeviceAdded(recoveryDevice);
                emit deviceChange();
            });
}
#endif

AppContext::~AppContext()
{
    m_devices.clear();

#ifdef ENABLE_RECOVERY_DEVICE_SUPPORT
    for (auto recoveryDevice : m_recoveryDevices) {
        emit recoveryDeviceRemoved(recoveryDevice->ecid);
        delete recoveryDevice;
    }
#endif
}

void AppContext::setCurrentDeviceSelection(const DeviceSelection &selection,
                                           bool showConnectedDevices)
{
    if (m_currentSelection.type == selection.type &&
        m_currentSelection.udid == selection.udid &&
        m_currentSelection.ecid == selection.ecid &&
        m_currentSelection.section == selection.section) {
        qDebug() << "setCurrentDeviceSelection: No change in selection";
        if (showConnectedDevices) {
            MainWindow::sharedInstance()->showConnectedDevicesTab();
        }
        return; // No change
    }
    m_currentSelection = selection;
    emit currentDeviceSelectionChanged(m_currentSelection);

    if (showConnectedDevices) {
        MainWindow::sharedInstance()->showConnectedDevicesTab();
    }
}

const DeviceSelection &AppContext::getCurrentDeviceSelection() const
{
    return m_currentSelection;
}

const iDescriptorDevice *
AppContext::getDeviceByMacAddress(const QString &macAddress) const
{
    for (const auto &device : m_devices) {
        if (device->deviceInfo.wifiMacAddress == macAddress.toStdString()) {
            return device.get();
        }
    }
    return nullptr;
}

void AppContext::cachePairingFile(const QString &uniq,
                                  const QString &pairingFilePath)
{
    m_pairingFileCache.insert(uniq, pairingFilePath);
}
const QString AppContext::getCachedPairingFile(const QString &uniq) const
{
    QString pairingFile;

    if (m_pairingFileCache.contains(uniq)) {
        pairingFile = m_pairingFileCache.value(uniq).toString();
    }
    return pairingFile;
}

void AppContext::heartbeatFailed(const QString &macAddress, int tries)
{
    emit deviceHeartbeatFailed(macAddress, tries);
}

void AppContext::tryToConnectToNetworkDevice(const NetworkDevice &device,
                                             bool forceCache,
                                             bool setSelectionIfExists)
{
    qDebug() << "Trying to connect to network device with MAC:"
             << device.macAddress << "IP:" << device.address;

    // force refresh macAddress-pairing file mapping
    if (forceCache) {
        cachePairedDevices();
    }

    const iDescriptorDevice *existingDevice = nullptr;
    existingDevice = getDeviceByMacAddress(device.macAddress);

    if (existingDevice) {
        emit deviceAlreadyExistsMAC(
            iDescriptor::Uniq(existingDevice->deviceInfo.wifiMacAddress, true));
        // TODO: add a setting for this
        if (setSelectionIfExists) {
            setCurrentDeviceSelection(DeviceSelection(existingDevice->udid),
                                      true);
        }
        return;
    }

    cachePairedDevices();
    QString pairing_file = getCachedPairingFile(device.macAddress);
    if (pairing_file.isEmpty()) {
        qDebug() << "No pairing file cached for device with MAC:"
                 << device.macAddress
                 << "Emitting noPairingFileForWirelessDevice event";
        emit noPairingFileForWirelessDevice(device.macAddress);
        return;
    }
    core->init_wireless_device(device.address, pairing_file, device.macAddress);
    emit initStarted(device.macAddress);
}

void AppContext::handlePairing(iDescriptor::Uniq uniq, bool timeout)
{
    m_pendingDevices.append(uniq);
    emit devicePasswordProtected(uniq);
    emit deviceChange();
    if (timeout) {
        QTimer::singleShot(
            SettingsManager::sharedInstance()->connectionTimeout() * 1000, this,
            [this, uniq]() {
                if (m_pendingDevices.contains(uniq)) {
                    qDebug() << "Pairing expired for "
                                "device UDID:"
                             << uniq;
                    m_pendingDevices.removeAll(uniq);
                    emit devicePairingExpired(uniq);
                    emit deviceChange();
                }
            });
    }
}