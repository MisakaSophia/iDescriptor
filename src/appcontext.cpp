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
#include "devicemonitor.h"
#include "iDescriptor.h"
#include "mainwindow.h"
// #include "settingsmanager.h"
#include "networkdevicemanager.h"
#include <QDebug>
#include <QMessageBox>
#include <QTimer>
#include <QUuid>

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

    // FIXME: windows and macOS support later
    QDir lockdowndir("/var/lib/lockdown");
    if (!lockdowndir.exists()) {
        return;
    }

    // Load cached pairing files
    QStringList pairingFiles =
        lockdowndir.entryList(QStringList() << "*.plist", QDir::Files);

    qDebug() << "Parsing cached pairing files in /var/lib/lockdown:";
    for (const QString &fileName : pairingFiles) {
        plist_t fileData = nullptr;
        plist_read_from_file(
            lockdowndir.filePath(fileName).toUtf8().constData(), &fileData,
            NULL);

        if (!fileData) {
            continue;
        }
        plist_print(fileData);
        const std::string wifiMacAddress =
            PlistNavigator(fileData)["WiFiMACAddress"].getString();
        // plist_free(fileData);
        bool isCompatible = !wifiMacAddress.empty();
        // TODO: !important invalidate old pairing files
        // sometimes there is no WiFiMACAddress
        if (!isCompatible) {
            continue;
        }
        qDebug() << "Found pairing file for MAC"
                 << QString::fromStdString(wifiMacAddress);

        qDebug() << "Caching pairing file for MAC"
                 << QString::fromStdString(wifiMacAddress) << "Local Path"
                 << lockdowndir.filePath(fileName);
        m_pairingFileCache[QString::fromStdString(wifiMacAddress)] =
            lockdowndir.filePath(fileName);
    }
}

void AppContext::addDevice(QString udid,
                           DeviceMonitorThread::IdeviceConnectionType conn_type,
                           AddType addType, QString wifiMacAddress)
{

    try {
        // iDescriptorInitDeviceResult initResult;
        auto initResult = std::make_shared<iDescriptorInitDeviceResult>();
        QFuture<void> future = QtConcurrent::run([this, udid, conn_type,
                                                  addType, wifiMacAddress,
                                                  initResult]() {
            if (addType == AddType::UpgradeToWireless) {
                // udid is mac address here
                const QString _pairingFilePath = getCachedPairingFile(udid);

                if (_pairingFilePath.isEmpty()) {
                    qDebug() << "Cannot upgrade to wireless, no cached pairing "
                                "file for"
                             << udid;
                    return;
                }

                QFile pairingFilePath(_pairingFilePath);
                if (!pairingFilePath.exists()) {
                    qDebug()
                        << "Cannot upgrade to wireless, no pairing file for"
                        << udid;
                    return;
                }
                pairingFilePath.close();

                QList<NetworkDevice> networkDevices =
                    NetworkDeviceManager::sharedInstance()
                        ->m_networkProvider->getNetworkDevices();

                auto it = std::find_if(
                    networkDevices.constBegin(), networkDevices.constEnd(),
                    [wifiMacAddress](const NetworkDevice &device) {
                        return device.macAddress.compare(
                                   wifiMacAddress, Qt::CaseInsensitive) == 0;
                    });

                if (it != networkDevices.constEnd()) {

                    *initResult = init_idescriptor_device(
                        udid, {it->address, pairingFilePath.fileName()});
                } else {
                    qDebug() << "No network device found with MAC address:"
                             << wifiMacAddress;
                    return;
                }
            } else if (addType == AddType::Wireless) {
                // FIXME: its not udid here its macAddress
                const QString _pairingFilePath = getCachedPairingFile(udid);

                if (_pairingFilePath.isEmpty()) {
                    qDebug() << "Cannot upgrade to wireless, no cached pairing "
                                "file for"
                             << udid;
                    return;
                }

                QFile pairingFilePath(_pairingFilePath);
                if (!pairingFilePath.exists()) {
                    qDebug()
                        << "Cannot upgrade to wireless, no pairing file for"
                        << udid;
                    return;
                }
                pairingFilePath.close();

                QList<NetworkDevice> networkDevices =
                    NetworkDeviceManager::sharedInstance()
                        ->m_networkProvider->getNetworkDevices();

                // todo : retry logic if not found
                auto it = std::find_if(
                    networkDevices.constBegin(), networkDevices.constEnd(),
                    [wifiMacAddress](const NetworkDevice &device) {
                        return device.macAddress.compare(
                                   wifiMacAddress, Qt::CaseInsensitive) == 0;
                    });

                if (it != networkDevices.constEnd()) {
                    *initResult = init_idescriptor_device(
                        udid, {it->address, pairingFilePath.fileName()});
                } else {
                    qDebug() << "No network device found with MAC address:"
                             << wifiMacAddress;
                    return;
                }
            }

            else {

                *initResult = init_idescriptor_device(udid, {nullptr, nullptr});
            }
        });
        QFutureWatcher<void> *watcher = new QFutureWatcher<void>();
        watcher->setFuture(future);
        connect(watcher, &QFutureWatcher<void>::finished, this,
                [this, udid, initResult, addType, conn_type, watcher]() {
                    watcher->deleteLater();
                    qDebug() << "init_idescriptor_device success ?: "
                             << initResult->success;
                    // qDebug() << "init_idescriptor_device error code: " <<
                    // initResult.error;
                    if (!initResult->success) {
                        qDebug() << "Failed to initialize device with UDID: "
                                 << udid;
                        return;
                    }
                    // if (!initResult.success) {
                    //     qDebug() << "Failed to initialize device with UDID: "
                    //     << udid; if (initResult.error ==
                    //     LOCKDOWN_E_PASSWORD_PROTECTED)
                    //     {
                    //         if (addType == AddType::Regular) {
                    //             m_pendingDevices.append(udid);
                    //             emit devicePasswordProtected(udid);
                    //             emit deviceChange();
                    //             QTimer::singleShot(
                    //                 SettingsManager::sharedInstance()->connectionTimeout()
                    //                 *
                    //                     1000,
                    //                 this, [this, udid]() {
                    //                     if (m_pendingDevices.contains(udid))
                    //                     {
                    //                         qDebug() << "Pairing expired for
                    //                         device UDID:
                    //                         "
                    //                                  << udid;
                    //                         m_pendingDevices.removeAll(udid);
                    //                         emit devicePairingExpired(udid);
                    //                         emit deviceChange();
                    //                     }
                    //                 });
                    //         }
                    //     } else if (initResult.error ==
                    //                    LOCKDOWN_E_PAIRING_DIALOG_RESPONSE_PENDING
                    //                    ||
                    //                initResult.error ==
                    //                LOCKDOWN_E_INVALID_HOST_ID) {
                    //         m_pendingDevices.append(udid);
                    //         emit devicePairPending(udid);
                    //         emit deviceChange();
                    //         QTimer::singleShot(
                    //             SettingsManager::sharedInstance()->connectionTimeout()
                    //             *
                    //                 1000,
                    //             this, [this, udid]() {
                    //                 qDebug()
                    //                     << "Pairing timer fired for device
                    //                     UDID: " << udid;
                    //                 if (m_pendingDevices.contains(udid)) {
                    //                     qDebug()
                    //                         << "Pairing expired for device
                    //                         UDID: " << udid;
                    //                     m_pendingDevices.removeAll(udid);
                    //                     emit devicePairingExpired(udid);
                    //                     emit deviceChange();
                    //                 }
                    //             });
                    //     } else {
                    //         qDebug() << "Unhandled error for device UDID: "
                    //         << udid
                    //                  << " Error code: " << initResult.error;
                    //     }
                    //     return;
                    // }
                    qDebug() << "Device initialized: " << udid;

                    iDescriptorDevice *device = new iDescriptorDevice{
                        .udid = udid.toStdString(),
                        .conn_type = conn_type,
                        .provider = initResult->provider,
                        .deviceInfo = initResult->deviceInfo,
                        .afcClient = initResult->afcClient,
                        .afc2Client = initResult->afc2Client,
                        .lockdown = initResult->lockdown,
                        .mutex = new std::recursive_mutex(),
                        .imageMounter = initResult->imageMounter,
                        .diagRelay = initResult->diagRelay,
                        .locationSimulation = initResult->locationSimulation};
                    m_devices[device->udid] = device;
                    if (addType == AddType::Regular) {
                        qDebug() << "Regular device added: " << udid;
                        // SettingsManager::sharedInstance()->doIfEnabled(
                        //     SettingsManager::Setting::AutoRaiseWindow, []() {
                        //         if (MainWindow *mainWindow =
                        //         MainWindow::sharedInstance()) {
                        //             mainWindow->raise();
                        //             mainWindow->activateWindow();
                        //         }
                        //     });

                        emit deviceAdded(device);
                        emit deviceChange();
                        return;
                    }
                    emit devicePaired(device);
                    emit deviceChange();
                    m_pendingDevices.removeAll(udid);
                });
    } catch (const std::exception &e) {
        qDebug() << "Exception in onDeviceAdded: " << e.what();
    }
}

int AppContext::getConnectedDeviceCount() const
{
    // #ifdef ENABLE_RECOVERY_DEVICE_SUPPORT
    //     return m_devices.size() + m_recoveryDevices.size();
    // #else
    return m_devices.size();
    // #endif
}

/*
    FIXME:
    on macOS, sometimes you get wireless disconnects even though we are not
    listening for wireless devices it does not have any to do with us, but
   it still happens so be aware of that
    */
void AppContext::removeDevice(QString _udid)
{
    const std::string udid = _udid.toStdString();
    qDebug() << "AppContext::removeDevice device with UUID:"
             << QString::fromStdString(udid);

    if (m_pendingDevices.contains(_udid)) {
        m_pendingDevices.removeAll(_udid);
        emit devicePairingExpired(_udid);
        emit deviceChange();
        return;
    } else {
        qDebug() << "Device with UUID " + _udid +
                        " not found in pending devices.";
    }

    if (!m_devices.contains(udid)) {
        qDebug() << "Device with UUID " + _udid +
                        " not found in normal devices.";
        return;
    }

    iDescriptorDevice *device = m_devices[udid];
    m_devices.remove(udid);

    emit deviceRemoved(udid, device->deviceInfo.wifiMacAddress);
    emit deviceChange();

    // std::lock_guard<std::recursive_mutex> lock(*device->mutex);

    // if (device->afcClient)
    //     afc_client_free(device->afcClient);
    // if (device->afc2Client)
    //     afc_client_free(device->afc2Client);
    // idevice_free(device->device);
    // delete device->mutex;
    // delete device;
}

#ifdef ENABLE_RECOVERY_DEVICE_SUPPORT
void AppContext::removeRecoveryDevice(uint64_t ecid)
{
    // if (!m_recoveryDevices.contains(ecid)) {
    //     qDebug() << "Device with ECID " + QString::number(ecid) +
    //                     " not found. Please report this issue.";
    //     return;
    // }

    // qDebug() << "Removing recovery device with ECID:" << ecid;

    // // Fix use-after-free: get pointer before removing from map
    // iDescriptorRecoveryDevice *deviceInfo =
    // m_recoveryDevices.value(ecid); m_recoveryDevices.remove(ecid);

    // emit recoveryDeviceRemoved(ecid);
    // emit deviceChange();

    // std::lock_guard<std::recursive_mutex> lock(*deviceInfo->mutex);
    // delete deviceInfo->mutex;
    // delete deviceInfo;
}
#endif

iDescriptorDevice *AppContext::getDevice(const std::string &udid)
{
    return m_devices.value(udid, nullptr);
}

QList<iDescriptorDevice *> AppContext::getAllDevices()
{
    return m_devices.values();
}

// #ifdef ENABLE_RECOVERY_DEVICE_SUPPORT
// QList<iDescriptorRecoveryDevice *> AppContext::getAllRecoveryDevices()
// {
//     // return m_recoveryDevices.values();
// }
// #endif

// Returns whether there are any devices connected (regular or recovery)
bool AppContext::noDevicesConnected() const
{
    // #ifdef ENABLE_RECOVERY_DEVICE_SUPPORT
    //     return (m_devices.isEmpty() && m_recoveryDevices.isEmpty() &&
    //             m_pendingDevices.isEmpty());
    // #else
    return (m_devices.isEmpty() && m_pendingDevices.isEmpty());
    // #endif
}

#ifdef ENABLE_RECOVERY_DEVICE_SUPPORT
void AppContext::addRecoveryDevice(uint64_t ecid)
{
    // iDescriptorInitDeviceResultRecovery res =
    //     init_idescriptor_recovery_device(ecid);

    // if (!res.success) {
    //     qDebug() << "Failed to initialize recovery device with ECID: "
    //              << QString::number(ecid);
    //     qDebug() << "Error code: " << res.error;
    //     return;
    // }

    // iDescriptorRecoveryDevice *recoveryDevice = new
    // iDescriptorRecoveryDevice(); recoveryDevice->ecid =
    // res.deviceInfo.ecid; recoveryDevice->mode = res.mode;
    // recoveryDevice->cpid = res.deviceInfo.cpid;
    // recoveryDevice->bdid = res.deviceInfo.bdid;
    // recoveryDevice->displayName = res.displayName;
    // recoveryDevice->mutex = new std::recursive_mutex();

    // m_recoveryDevices[res.deviceInfo.ecid] = recoveryDevice;
    // emit recoveryDeviceAdded(recoveryDevice);
    // emit deviceChange();
}
#endif

AppContext::~AppContext()
{
    //     for (auto device : m_devices) {
    //         emit deviceRemoved(device->udid);
    //         if (device->afcClient)
    //             afc_client_free(device->afcClient);
    //         if (device->afc2Client)
    //             afc_client_free(device->afc2Client);
    //         idevice_free(device->device);
    //         delete device->mutex;
    //         delete device;
    //     }

    // #ifdef ENABLE_RECOVERY_DEVICE_SUPPORT
    //     for (auto recoveryDevice : m_recoveryDevices) {
    //         emit recoveryDeviceRemoved(recoveryDevice->ecid);
    //         delete recoveryDevice->mutex;
    //         delete recoveryDevice;
    //     }
    // #endif
}

void AppContext::setCurrentDeviceSelection(const DeviceSelection &selection)
{
    qDebug() << "New selection -"
             << " Type:" << selection.type
             << " UDID:" << QString::fromStdString(selection.udid)
             << " ECID:" << selection.ecid << " Section:" << selection.section;
    if (m_currentSelection.type == selection.type &&
        m_currentSelection.udid == selection.udid &&
        m_currentSelection.ecid == selection.ecid &&
        m_currentSelection.section == selection.section) {
        qDebug() << "setCurrentDeviceSelection: No change in selection";
        return; // No change
    }
    m_currentSelection = selection;
    emit currentDeviceSelectionChanged(m_currentSelection);
}

const DeviceSelection &AppContext::getCurrentDeviceSelection() const
{
    return m_currentSelection;
}

const iDescriptorDevice *
AppContext::getDeviceByMacAddress(const QString &macAddress) const
{
    for (const iDescriptorDevice *device : m_devices) {
        if (device->deviceInfo.wifiMacAddress == macAddress.toStdString()) {
            return device;
        }
    }
    return nullptr;
}

void AppContext::cachePairingFile(const QString &udid,
                                  const QString &pairingFilePath)
{
    m_pairingFileCache.insert(udid, pairingFilePath);
}
const QString AppContext::getCachedPairingFile(const QString &udid) const
{
    QString pairingFile;

    // Retrieve the pairing file from the cache
    if (m_pairingFileCache.contains(udid)) {
        pairingFile = m_pairingFileCache.value(udid);
    }

    return pairingFile;
}

void AppContext::heartbeatFailed(const QString &macAddress, int tries)
{
    emit deviceHeartbeatFailed(macAddress, tries);
}