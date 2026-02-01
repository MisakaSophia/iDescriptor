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
    cachePairedDevices();
}

void AppContext::cachePairedDevices()
{

/*
 does not work on macOS because we cannot read /var/db/lockdown without root perm
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
        plist_print(fileData);
        const std::string wifiMacAddress =
            PlistNavigator(fileData)["WiFiMACAddress"].getString();
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
    // FIXME: implement caching for macOS
    /* MacOS */
    qDebug() << "Caching paired network devices from usbmuxd";
    auto conn = UsbmuxdConnection::default_new(0);
    if (conn.is_err()) {
        qDebug() << "ERROR: Failed to connect to usbmuxd!";
        // return 1;
    }

    auto devices = conn.unwrap().get_devices();
    if (devices.is_err()) {
        qDebug() << "ERROR: Failed to get device list!";
        // return 1;
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
                                m_pairingFileCache[QString::fromUtf8(
                                    mac_address)] =
                                    QString::fromStdString("/var/db/lockdown/" +
                                                           udid.unwrap() +
                                                           ".plist");
                                free(mac_address);
                            }
                        }

                        plist_free(root_node);
                    }
                    free(plist_data);
                }
                // qDebug() << "Wireless device UDID:"
                //          << QString::fromStdString(udid.unwrap())
                //          << "Pairing file retrieval"
                //          << (error == nullptr
                //                  ? "succeeded"
                //                  : QString::fromStdString(error->message));
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
#endif
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
                        udid, {it->address, _pairingFilePath});
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
                        udid, {it->address, _pairingFilePath});
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
                        .imageMounter = initResult->imageMounter,
                        .diagRelay = initResult->diagRelay,
                        .locationSimulation = initResult->locationSimulation,
                        .heartbeatThread = initResult->heartbeatThread};
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

    std::lock_guard<std::recursive_mutex> lock(device->mutex);

    if (device->afcClient)
        afc_client_free(device->afcClient);
    if (device->afc2Client)
        afc_client_free(device->afc2Client);
    // idevice_free(device->device);

    if (device->heartbeatThread) {
        device->heartbeatThread->requestInterruption();
        device->heartbeatThread->wait();
        delete device->heartbeatThread;
    }

    delete device;
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
    // FIXME: mutex?
    for (auto device : m_devices) {
        emit deviceRemoved(device->udid, device->deviceInfo.wifiMacAddress);
        if (device->afcClient)
            afc_client_free(device->afcClient);
        if (device->afc2Client)
            afc_client_free(device->afc2Client);
        // idevice_free(device->device);

        if (device->heartbeatThread) {
            device->heartbeatThread->requestInterruption();
            device->heartbeatThread->wait();
            delete device->heartbeatThread;
        }
    }

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
    } else {
        // FIXME: mac support
        // IdevicePairingFile* pairing_file = nullptr;
        // const char* file_path = udid.toUtf8().constData();
        // IdeviceFfiError* err = idevice_pairing_file_read(file_path,
        // &pairing_file); if (err == nullptr || pairing_file == nullptr) {
        //     pairingFile = QString::fromUtf8(file_path);
        // } else {
        //     qDebug() << "Failed to read pairing file for UDID:" << udid <<
        //     "Error:" << err->message; idevice_error_free(err);
        // }
    }

    return pairingFile;
}

void AppContext::heartbeatFailed(const QString &macAddress, int tries)
{
    emit deviceHeartbeatFailed(macAddress, tries);
}

void AppContext::tryToConnectToNetworkDevice(const QString &macAddress)
{

    // force refresh macAddress-udid mapping
    cachePairedDevices();

    QList<NetworkDevice> networkDevices =
        NetworkDeviceManager::sharedInstance()
            ->m_networkProvider->getNetworkDevices();

    auto it =
        std::find_if(networkDevices.constBegin(), networkDevices.constEnd(),
                     [macAddress](const NetworkDevice &device) {
                         return device.macAddress.compare(
                                    macAddress, Qt::CaseInsensitive) == 0;
                     });

    if (it != networkDevices.constEnd()) {
        QMetaObject::invokeMethod(
            AppContext::sharedInstance(), "addDevice", Qt::QueuedConnection,
            Q_ARG(QString, macAddress),
            Q_ARG(DeviceMonitorThread::IdeviceConnectionType,
                  DeviceMonitorThread::CONNECTION_NETWORK),
            Q_ARG(AddType, AddType::Wireless), Q_ARG(QString, macAddress));
    } else {
        qDebug() << "No network device found with MAC address:" << macAddress;
    }
}