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

/*
 does not work on macOS because we cannot read /var/db/lockdown without root
 perm
*/
#ifndef __APPLE__
    QDir lockdowndir(LOCKDOWN_PATH);
    if (!lockdowndir.exists()) {
        return;
    }

    // Load cached pairing files
    QStringList pairingFiles =
        lockdowndir.entryList(QStringList() << "*.plist", QDir::Files);

    qDebug() << "Parsing cached pairing files in /var/lib/lockdown:";
    for (const QString &fileName : pairingFiles) {
        qDebug() << "Found pairing file:" << fileName;
        plist_t fileData = nullptr;
        plist_read_from_file(
            lockdowndir.filePath(fileName).toUtf8().constData(), &fileData,
            NULL);

        if (!fileData) {
            continue;
        }
        const std::string wifiMacAddress =
            PlistNavigator(fileData)["WiFiMACAddress"].getString();
        // FIXME: free ?
        // plist_free(fileData);
        bool isCompatible = !wifiMacAddress.empty();
        // TODO: !important invalidate expired pairing files
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
#else
    /* MacOS */
    qDebug() << "Caching paired network devices from usbmuxd";
    auto conn = UsbmuxdConnection::default_new(0);
    if (conn.is_err()) {
        qDebug() << "ERROR: Failed to connect to usbmuxd!";
        return;
    }

    auto devices = conn.unwrap().get_devices();
    if (devices.is_err()) {
        qDebug() << "ERROR: Failed to get device list!";
        return;
    }

    for (const auto &device : devices.unwrap()) {
        auto conn_type = device.get_connection_type();
        if (conn_type.is_some() &&
            conn_type.unwrap().to_string() == "Network") {
            auto udid = device.get_udid();
            if (udid.is_some()) {
                qDebug() << "Found network device with UDID:"
                         << QString::fromStdString(udid.unwrap());

                auto pairing_file =
                    conn.unwrap().get_pair_record(udid.unwrap());

                uint8_t *plist_data = nullptr;
                size_t plist_size = 0;
                IdeviceFfiError *error = idevice_pairing_file_serialize(
                    pairing_file.unwrap().raw(), &plist_data, &plist_size);

                if (error == nullptr) {
                    plist_t root_node = nullptr;
                    plist_from_xml((const char *)plist_data, plist_size,
                                   &root_node);

                    if (root_node) {
                        plist_t wifi_mac_node =
                            plist_dict_get_item(root_node, "WiFiMACAddress");

                        if (wifi_mac_node &&
                            plist_get_node_type(wifi_mac_node) ==
                                PLIST_STRING) {
                            char *mac_address = nullptr;
                            plist_get_string_val(wifi_mac_node, &mac_address);

                            if (mac_address) {
                                qDebug() << "Adding to cache"
                                         << QString::fromUtf8(mac_address);
                                QString path = QString::fromStdString(
                                    "/var/db/lockdown/" + udid.unwrap() +
                                    ".plist");
                                SettingsManager::sharedInstance()
                                    ->setIdeviceDefaultPairingFile(
                                        QString::fromUtf8(mac_address), path);
                                m_pairingFileCache[QString::fromUtf8(
                                    mac_address)] = path;
                                plist_mem_free(mac_address);
                            }
                        }

                        plist_free(root_node);
                    }
                    plist_mem_free(plist_data);
                }
                // Clean up
                // idevice_pairing_file_free(pairing_file.unwrap().raw());
            }
        } else if (conn_type.is_some()) {
            auto udid = device.get_udid();
            if (udid.is_some()) {
                qDebug() << "Found USB device with UDID:"
                         << QString::fromStdString(udid.unwrap());
            }
        }
    }

    // sometimes macOS doesn't find the network iDevice even tho we can connect
    // if we know the ip address
    QMap<QString, QString> cachedPairingFiles =
        SettingsManager::sharedInstance()->getAllIdeviceDefaultPairingFiles();

    for (const QString &mac : cachedPairingFiles.keys()) {
        const QString path = cachedPairingFiles.value(mac);
        qDebug() << "Using pairing file for MAC:" << mac
                 << "cached from settings";
        m_pairingFileCache[mac] = path;
    }

#endif
}

void AppContext::addDevice(iDescriptor::Uniq uniq,
                           iDescriptor::IdeviceConnectionType conn_type,
                           AddType addType, QString wifiMacAddress,
                           QString ipAddress)
{

    if (QCoreApplication::closingDown()) {
        qDebug() << "Ignoring addDevice during shutdown for" << uniq.get();
        return;
    }
    emit initStarted(uniq);

    if (auto device = getDevice(uniq)) {
        emit deviceAlreadyExists(uniq);
        return;
    }

    try {
        auto initResult = std::make_shared<iDescriptorInitDeviceResult>();

        QFuture<void> future = QtConcurrent::run([this, uniq, conn_type,
                                                  addType, wifiMacAddress,
                                                  ipAddress, initResult]() {
            if (addType == AddType::UpgradeToWireless) {
                qDebug() << "AddType::UpgradeToWireless";
                const QString _pairingFilePath = getCachedPairingFile(uniq);

                if (_pairingFilePath.isEmpty()) {
                    qDebug() << "Cannot upgrade to wireless, no cached pairing "
                                "file for"
                             << uniq;
                    emitNoPairingFileForWirelessDevice(uniq);
                    return;
                }

                init_idescriptor_device(uniq, *initResult,
                                        {ipAddress, _pairingFilePath});

            } else if (addType == AddType::Wireless) {
                qDebug() << "AddType::Wireless";
                const QString _pairingFilePath = getCachedPairingFile(uniq);

                if (_pairingFilePath.isEmpty()) {
                    qDebug() << "Cannot upgrade to wireless, no cached pairing "
                                "file for"
                             << uniq;
                    emitNoPairingFileForWirelessDevice(uniq);
                    return;
                }

                init_idescriptor_device(uniq, *initResult,
                                        {ipAddress, _pairingFilePath});

            }

            else {
                qDebug() << "AddType::Regular";
                init_idescriptor_device(uniq, *initResult, {nullptr, nullptr});
            }
        });
        QFutureWatcher<void> *watcher = new QFutureWatcher<void>();
        watcher->setFuture(future);
        connect(
            watcher, &QFutureWatcher<void>::finished, this,
            [this, uniq, initResult, addType, conn_type, watcher]() mutable {
                watcher->deleteLater();
                qDebug() << "init_idescriptor_device success ?: "
                         << initResult->success;

                if (!initResult->success) {
                    return handlePairing(initResult.get(), addType, uniq);
                }
                qDebug() << "Device initialized: " << uniq;

                iDescriptorDevice *device = new iDescriptorDevice{
                    .udid = initResult->deviceInfo.UniqueDeviceID,
                    .conn_type = conn_type,
                    .provider = initResult->provider,
                    .deviceInfo = initResult->deviceInfo,
                    .afcClient = initResult->afcClient,
                    .afc2Client = initResult->afc2Client,
                    .lockdown = initResult->lockdown,
                    .diagRelay = initResult->diagRelay,
                    .heartbeatThread = initResult->heartbeatThread};

                if (initResult->deviceInfo.isWireless &&
                    initResult->heartbeatThread) {
                    device->heartbeatThread->start();
                }

                m_devices[device->udid] = device;

                if (addType == AddType::Wireless ||
                    addType == AddType::UpgradeToWireless ||
                    addType == AddType::Regular) {
                    qDebug() << "Wireless device added: " << uniq;

                    emit deviceAdded(device);
                    emit deviceChange();
                    return;
                }
                emit devicePaired(device);
                emit deviceChange();
                m_pendingDevices.removeAll(uniq);
            });
    } catch (const std::exception &e) {
        qDebug() << "Exception in onDeviceAdded: " << e.what();
    }
}

int AppContext::getConnectedDeviceCount() const
{
#ifdef ENABLE_RECOVERY_DEVICE_SUPPORT
    return m_devices.size() + m_recoveryDevices.size();
#else
    return m_devices.size();
#endif
}

void AppContext::removeDevice(iDescriptor::Uniq uniq)
{
    qDebug() << "AppContext::removeDevice device with"
             << (uniq.isMac() ? "MAC" : "UDID") << uniq.get();

    std::string udid = uniq.isUdid() ? uniq.get().toStdString() : "";
    QString q_udid = QString::fromStdString(udid);

    if (uniq.isMac()) {
        const iDescriptorDevice *device = getDeviceByMacAddress(uniq.get());
        if (device) {
            udid = device->udid;
            q_udid = QString::fromStdString(udid);
        } else {
            qDebug() << "Device with MAC " << uniq << " not found.";
        }
    }

    if (m_pendingDevices.contains(q_udid)) {
        m_pendingDevices.removeAll(q_udid);
        emit devicePairingExpired(q_udid);
        emit deviceChange();
        return;
    } else {
        qDebug() << "Device with UUID " + q_udid +
                        " not found in pending devices.";
    }

    if (!m_devices.contains(udid)) {
        qDebug() << "Device with UUID " + q_udid +
                        " not found in normal devices.";
        return;
    }

    iDescriptorDevice *device = m_devices[udid];
    m_devices.remove(udid);

    emit deviceRemoved(udid, device->deviceInfo.wifiMacAddress,
                       device->deviceInfo.ipAddress,
                       device->deviceInfo.isWireless);
    emit deviceChange();

    qDebug() << "Waiting to acquire lock for device cleanup: "
             << QString::fromStdString(udid);
    std::lock_guard<std::recursive_mutex> lock(device->mutex);
    qDebug() << "Acquired lock, cleaning up device: "
             << QString::fromStdString(udid);

    freeDevice(device);
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

iDescriptorDevice *AppContext::getDevice(const std::string &uniq)
{
    return m_devices.value(uniq, nullptr);
}

QList<iDescriptorDevice *> AppContext::getAllDevices()
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
    for (auto device : m_devices) {
        freeDevice(device);
    }

    m_devices.clear();

#ifdef ENABLE_RECOVERY_DEVICE_SUPPORT
    for (auto recoveryDevice : m_recoveryDevices) {
        emit recoveryDeviceRemoved(recoveryDevice->ecid);
        delete recoveryDevice;
    }
#endif
}

void AppContext::setCurrentDeviceSelection(const DeviceSelection &selection)
{
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

void AppContext::cachePairingFile(const QString &uniq,
                                  const QString &pairingFilePath)
{
    m_pairingFileCache.insert(uniq, pairingFilePath);
}
const QString AppContext::getCachedPairingFile(const QString &uniq) const
{
    QString pairingFile;

    if (m_pairingFileCache.contains(uniq)) {
        pairingFile = m_pairingFileCache.value(uniq);
    }
    return pairingFile;
}

void AppContext::heartbeatFailed(const QString &macAddress, int tries)
{
    emit deviceHeartbeatFailed(macAddress, tries);
}

void AppContext::tryToConnectToNetworkDevice(const NetworkDevice &device)
{

    // force refresh macAddress-udid mapping
    cachePairedDevices();

    QMetaObject::invokeMethod(
        AppContext::sharedInstance(), "addDevice", Qt::QueuedConnection,
        Q_ARG(iDescriptor::Uniq, iDescriptor::Uniq(device.macAddress, true)),
        Q_ARG(iDescriptor::IdeviceConnectionType,
              iDescriptor::CONNECTION_NETWORK),
        Q_ARG(AddType, AddType::Wireless), Q_ARG(QString, device.macAddress),
        Q_ARG(QString, device.address));
}

// this is required because cannot emit signals from qfuture
void AppContext::emitNoPairingFileForWirelessDevice(const QString &udid)
{
    emit noPairingFileForWirelessDevice(udid);
}

void AppContext::freeDevice(iDescriptorDevice *device)
{
    if (device->afcClient)
        afc_client_free(device->afcClient);
    if (device->afc2Client)
        afc_client_free(device->afc2Client);

    if (device->heartbeatThread) {
        device->heartbeatThread->requestInterruption();
        device->heartbeatThread->wait();
        delete device->heartbeatThread;
    }

    lockdownd_client_free(device->lockdown);
    idevice_provider_free(device->provider);
    delete device;
    device = nullptr;
}

void AppContext::handlePairing(iDescriptorInitDeviceResult *initResult,
                               AddType addType, iDescriptor::Uniq uniq)
{
    qDebug() << "[handlePairing] for device" << uniq;
    emit initFailed(uniq);

    if (!initResult || !initResult->error) {
        qDebug() << "[handlePairing] initResult->error is null for" << uniq
                 << "- skipping pairing handling";
        return;
    }

    const auto code = initResult->error->code;
    if (code == PairingDialogResponsePending || code == InvalidHostID ||
        code == PasswordProtected) {
        if (addType == AddType::Regular) {
            m_pendingDevices.append(uniq);
            emit devicePasswordProtected(uniq);
            emit deviceChange();
            QTimer::singleShot(
                SettingsManager::sharedInstance()->connectionTimeout() * 1000,
                this, [this, uniq]() {
                    if (m_pendingDevices.contains(uniq)) {
                        qDebug() << "Pairing expired for "
                                    "device UDID:"
                                 << uniq;
                        m_pendingDevices.removeAll(uniq);
                        emit devicePairingExpired(uniq);
                        emit deviceChange();
                    }
                });
            // FIXME: free properly and move to a better place
            QThreadPool::globalInstance()->start([uniq, this]() {
                UsbmuxdConnectionHandle *usbmuxd_conn = nullptr;
                UsbmuxdAddrHandle *addr_handle = nullptr;
                IdeviceProviderHandle *provider = nullptr;
                LockdowndClientHandle *lockdown = nullptr;
                IdevicePairingFile *pairing_file = nullptr;

                IdeviceFfiError *err =
                    idevice_usbmuxd_new_default_connection(0, &usbmuxd_conn);
                if (err) {
                    // if (!isWireless) {
                    qDebug() << "Failed to connect to usbmuxd";
                    // goto cleanup;
                    return;
                    // }
                }

                err = idevice_usbmuxd_default_addr_new(&addr_handle);
                if (err) {
                    qDebug() << "Failed to create address handle";
                    // goto cleanup;
                    return;
                }

                UsbmuxdDeviceHandle **devices;
                int device_count;
                int actual_device_id = -1;
                err = idevice_usbmuxd_get_devices(usbmuxd_conn, &devices,
                                                  &device_count);

                for (size_t i = 0; i < device_count; i++) {
                    const char *device_udid =
                        idevice_usbmuxd_device_get_udid(devices[i]);
                    if (strcmp(device_udid, uniq.get().toUtf8().constData()) ==
                        0) {
                        actual_device_id =
                            idevice_usbmuxd_device_get_device_id(devices[i]);
                        break;
                    }
                }

                err = usbmuxd_provider_new(
                    addr_handle, 0, uniq.get().toUtf8().constData(),
                    actual_device_id, APP_LABEL, &provider);

                err = lockdownd_connect(provider, &lockdown);
                if (err) {
                    qDebug() << "Failed to connect to lockdown";
                    return;
                }

                QString hostId = QUuid::createUuid()
                                     .toString()
                                     .remove("{")
                                     .remove("}")
                                     .toUpper();
                char *buid = nullptr;
                idevice_usbmuxd_get_buid(usbmuxd_conn, &buid);

                bool ok = false;
                while (true) {
                    // if (const auto dev =
                    //         AppContext::sharedInstance()
                    //             ->getDevice(
                    //                 uniq.get().toStdString()))
                    //                 {

                    if (ok) {
                        qDebug() << "Successfully paired with "
                                    "device, ";
                        break;
                    };
                    err = lockdownd_pair(lockdown, hostId.toStdString().c_str(),
                                         buid, nullptr, &pairing_file);
                    if (err) {
                        qDebug()
                            << "Failed to pair with device" << err->message;

                        std::this_thread::sleep_for(std::chrono::seconds(5));
                        //  return;
                        // goto cleanup;
                    } else {

                        qDebug() << "There was no error, pairing "
                                    "successful";
                        uint8_t *data = nullptr;
                        size_t size = 0;

                        // Serialize pairing file to bytes
                        IdeviceFfiError *err = idevice_pairing_file_serialize(
                            pairing_file, &data, &size);
                        if (err) {
                            qDebug() << "Failed to serialize "
                                        "pairing file:"
                                     << err->message;
                            // idevice_error_free(err);
                            // goto cleanup;
                        }

                        err = idevice_usbmuxd_save_pair_record(
                            usbmuxd_conn, uniq.get().toUtf8().constData(), data,
                            size);

                        QMetaObject::invokeMethod(
                            AppContext::sharedInstance(), "addDevice",
                            Qt::QueuedConnection,
                            Q_ARG(iDescriptor::Uniq, uniq),
                            Q_ARG(iDescriptor::IdeviceConnectionType,
                                  iDescriptor::IdeviceConnectionType::
                                      CONNECTION_NETWORK),
                            Q_ARG(AddType, AddType::Regular));
                        ok = true;
                    }
                }
            });
        }
    }

    // else if (initResult.error ==
    //                LOCKDOWN_E_PAIRING_DIALOG_RESPONSE_PENDING
    //                ||
    //            initResult.error ==
    //            LOCKDOWN_E_INVALID_HOST_ID) {
    //     m_pendingDevices.append(udid);
    //     emit devicePairPending(udid);
    //     emit deviceChange();
    //     QTimer::singleShot(
    //         SettingsManager::sharedInstance()->connectionTimeout()
    //         *
    //             1000,
    //         this, [this, udid]() {
    //             qDebug()
    //                 << "Pairing timer fired for device
    //                 UDID: " << udid;
    //             if (m_pendingDevices.contains(udid)) {
    //                 qDebug()
    //                     << "Pairing expired for device
    //                     UDID: " << udid;
    //                 m_pendingDevices.removeAll(udid);
    //                 emit devicePairingExpired(udid);
    //                 emit deviceChange();
    //             }
    //         });
    // } else {
    //     qDebug() << "Unhandled error for device UDID: "
    //     << udid
    //              << " Error code: " << initResult.error;
    // }
    return;
}