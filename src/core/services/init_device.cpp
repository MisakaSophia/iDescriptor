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

#include "../../devicedatabase.h"
#include "../../iDescriptor.h"
// #include "../../servicemanager.h"
#include "../../appcontext.h"
#ifdef ENABLE_RECOVERY_DEVICE_SUPPORT
#include "libirecovery.h"
#endif
#include "../../heartbeat.h"
#include <QDebug>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sstream>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

std::string safeGetXML(const char *key, pugi::xml_node dict)
{
    for (pugi::xml_node child = dict.first_child(); child;
         child = child.next_sibling()) {
        if (strcmp(child.name(), "key") == 0 &&
            strcmp(child.text().as_string(), key) == 0) {
            pugi::xml_node value = child.next_sibling();
            if (value) {
                // Handle different XML element types
                if (strcmp(value.name(), "true") == 0) {
                    return "true";
                } else if (strcmp(value.name(), "false") == 0) {
                    return "false";
                } else if (strcmp(value.name(), "integer") == 0) {
                    return value.text().as_string();
                } else if (strcmp(value.name(), "string") == 0) {
                    return value.text().as_string();
                } else if (strcmp(value.name(), "real") == 0) {
                    return value.text().as_string();
                } else {
                    // For any other type, try to get the text content
                    return value.text().as_string();
                }
            }
        }
    }
    return "";
}

// this is reused in the ui in deviceinfowidget
void parseOldDeviceBattery(PlistNavigator &ioreg, DeviceInfo &d)
{
    d.batteryInfo.isCharging = ioreg["IsCharging"].getBool();

    d.batteryInfo.fullyCharged = ioreg["FullyCharged"].getBool();

    uint64_t appleRawCurrentCapacity =
        ioreg["AppleRawCurrentCapacity"].getUInt();
    uint64_t appleRawMaxCapacity = ioreg["AppleRawMaxCapacity"].getUInt();

    uint64_t oldCurrrentBatteryLevel =
        (appleRawCurrentCapacity && appleRawMaxCapacity)
            ? (appleRawCurrentCapacity * 100 / appleRawMaxCapacity)
            : 0;

    d.batteryInfo.currentBatteryLevel = oldCurrrentBatteryLevel;

    // adaptor details
    d.batteryInfo.usbConnectionType =
        ioreg["AdapterDetails"]["Description"].getString() == "usb type-c"
            ? BatteryInfo::ConnectionType::USB_TYPEC
            : BatteryInfo::ConnectionType::USB;
    d.batteryInfo.adapterVoltage = 0;

    // watt
    d.batteryInfo.watts = ioreg["AdapterDetails"]["Watts"].getUInt();
}

void parseOldDevice(PlistNavigator &ioreg, DeviceInfo &d)
{
    uint64_t cycleCount = ioreg["CycleCount"].getUInt();

    // skipping on very old devices for now
    std::string batterySerialNumber = "";
    uint64_t designCapacity = ioreg["DesignCapacity"].getUInt();

    uint64_t maxCapacity = ioreg["MaxCapacity"].getUInt();

    qDebug() << "Design capacity: " << designCapacity;
    qDebug() << "Max capacity: " << maxCapacity;

    // Compat
    int healthPercent =
        (designCapacity != 0) ? (maxCapacity * 100) / designCapacity : 0;
    healthPercent = std::min(healthPercent, 100);
    d.batteryInfo.health = QString::number(qBound(0, healthPercent, 100)) + "%";
    d.batteryInfo.cycleCount = cycleCount;
    d.batteryInfo.serialNumber = !batterySerialNumber.empty()
                                     ? batterySerialNumber
                                     : "Error retrieving serial number";

    parseOldDeviceBattery(ioreg, d);
}

// this is reused in the ui in deviceinfowidget
void parseDeviceBattery(PlistNavigator &ioreg, DeviceInfo &d)
{
    d.batteryInfo.isCharging = ioreg["IsCharging"].getBool();

    d.batteryInfo.fullyCharged = ioreg["FullyCharged"].getBool();

    qDebug() << "Stalebatteryinfo:"
             << ioreg["BatteryData"]["StateOfCharge"].getUInt();
    /* data is stale here so we need to calculate */
    // d.batteryInfo.currentBatteryLevel =
    //     ioreg["BatteryData"]["StateOfCharge"].getUInt();

    uint64_t appleRawCurrentCapacity =
        ioreg["AppleRawCurrentCapacity"].getUInt();
    uint64_t appleRawMaxCapacity = ioreg["AppleRawMaxCapacity"].getUInt();

    uint64_t currentBatteryLevel =
        (appleRawCurrentCapacity && appleRawMaxCapacity)
            ? (appleRawCurrentCapacity * 100 / appleRawMaxCapacity)
            : 0;

    d.batteryInfo.currentBatteryLevel = currentBatteryLevel;

    d.batteryInfo.usbConnectionType =
        ioreg["AdapterDetails"]["Description"].getString() == "usb type-c"
            ? BatteryInfo::ConnectionType::USB_TYPEC
            : BatteryInfo::ConnectionType::USB;

    // adaptor details
    d.batteryInfo.adapterVoltage =
        ioreg["AppleRawAdapterDetails"][0]["AdapterVoltage"].getUInt();

    d.batteryInfo.watts = ioreg["AppleRawAdapterDetails"][0]["Watts"].getUInt();
}

DeviceInfo fullDeviceInfo(const pugi::xml_document &doc,
                          AfcClientHandle *afcClient,
                          DiagnosticsRelay *diagRelay,
                          iDescriptorInitDeviceResult &result)
{
    pugi::xml_node dict = doc.child("plist").child("dict");
    auto safeGet = [&](const char *key) -> std::string {
        for (pugi::xml_node child = dict.first_child(); child;
             child = child.next_sibling()) {
            if (strcmp(child.name(), "key") == 0 &&
                strcmp(child.text().as_string(), key) == 0) {
                pugi::xml_node value = child.next_sibling();
                if (value)
                    return value.text().as_string();
            }
        }
        return "";
    };

    auto safeGetBool = [&](const char *key) -> bool {
        for (pugi::xml_node child = dict.first_child(); child;
             child = child.next_sibling()) {
            if (strcmp(child.name(), "key") == 0 &&
                strcmp(child.text().as_string(), key) == 0) {
                pugi::xml_node value = child.next_sibling();
                if (value && strcmp(value.name(), "true") == 0)
                    return true;
                else
                    return false;
            }
        }
        return false;
    };
    DeviceInfo &d = result.deviceInfo;
    d.deviceName = safeGet("DeviceName");
    d.deviceClass = safeGet("DeviceClass");
    d.deviceColor = safeGet("DeviceColor");
    d.modelNumber = safeGet("ModelNumber");
    d.cpuArchitecture = safeGet("CPUArchitecture");
    d.buildVersion = safeGet("BuildVersion");
    d.hardwareModel = safeGet("HardwareModel");
    d.hardwarePlatform = safeGet("HardwarePlatform");
    d.ethernetAddress = safeGet("EthernetAddress");
    d.bluetoothAddress = safeGet("BluetoothAddress");
    d.firmwareVersion = safeGet("FirmwareVersion");
    d.productVersion = safeGet("ProductVersion");
    d.wifiMacAddress = safeGet("WiFiAddress");

    QString q_version = QString::fromStdString(d.productVersion);
    QStringList parts = q_version.split('.');

    unsigned int major = (parts.length() > 0) ? parts[0].toInt() : 0;
    unsigned int minor = (parts.length() > 1) ? parts[1].toInt() : 0;
    unsigned int patch = (parts.length() > 2) ? parts[2].toInt() : 0;

    d.parsedDeviceVersion =
        DeviceVersion{.major = major, .minor = minor, .patch = patch};

    /*DiskInfo*/
    try {
        auto safeParseU64 = [&](const char *key) -> uint64_t {
            std::string s = safeGet(key);
            if (s.empty())
                return 0;
            try {
                return std::stoull(s);
            } catch (...) {
                qDebug() << "Failed to parse key to uint64_t:" << key
                         << "value:" << QString::fromStdString(s);
                return 0;
            }
        };
        d.diskInfo.totalDiskCapacity = safeParseU64("TotalDiskCapacity");
        d.diskInfo.totalDataCapacity = safeParseU64("TotalDataCapacity");
        d.diskInfo.totalSystemCapacity = safeParseU64("TotalSystemCapacity");
        /*
            For some reason this is way inaccrutate for iOS 17 and up
        */
        d.diskInfo.totalDataAvailable = safeParseU64("TotalDataAvailable");

        try {
            /*
                Example : this data seems to be the most accurate
            */
            //"Model: iPhone12,8"
            // "FSTotalBytes: 63966400512"
            // "FSFreeBytes: 2867101696"
            // "FSBlockSize: 4096"
            // FIXME: it's too slow on older devices?
            AfcDeviceInfo *info = new AfcDeviceInfo();
            qDebug() << "afc_get_device_info...";
            IdeviceFfiError *err = afc_get_device_info(afcClient, info);
            if (err) {
                qDebug() << "AFC get device info error code: " << err->message;
                return d;
            }
            if (info) {

                qDebug() << "AFC Disk Info" << info->free_bytes;
                d.diskInfo.totalDataAvailable = info->free_bytes;
            }
            // FIXME: free
            afc_device_info_free(info);
        } catch (const std::exception &e) {
            qDebug() << "Error parsing disk info: " << e.what();
        }
    } catch (const std::exception &e) {
        qDebug() << e.what();
        /*It's ok if any of those fails*/
    }

    std::string _activationState = safeGet("ActivationState");

    /* older devices dont have fusing status lets default to ProductionSOC for
     * now*/
    // std::string fStatus = safeGet("FusingStatus");
    // d.productionDevice = std::stoi(fStatus.empty() ? "0" : fStatus) == 3;

    d.productionDevice = safeGetBool("ProductionSOC");
    if (_activationState == "Activated") {
        d.activationState = DeviceInfo::ActivationState::Activated;
        // IOS 6
    } else if (_activationState == "WildcardActivated") {
        d.activationState =
            DeviceInfo::ActivationState::Activated; // Treat as activated
    } else if (_activationState == "FactoryActivated") {
        d.activationState = DeviceInfo::ActivationState::FactoryActivated;
    } else if (_activationState == "Unactivated") {
        d.activationState = DeviceInfo::ActivationState::Unactivated;
    } else {
        d.activationState =
            DeviceInfo::ActivationState::Unactivated; // Default value
    }
    std::string regionInfo = safeGet("RegionInfo");
    d.regionRaw = regionInfo;
    d.region = DeviceDatabase::parseRegionInfo(regionInfo);
    std::string rawProductType = safeGet("ProductType");
    const DeviceDatabaseInfo *info =
        DeviceDatabase::findByIdentifier(rawProductType);
    d.productType =
        info ? info->displayName ? info->displayName : info->marketingName
             : "Unknown Device";
    d.marketingName = info ? info->marketingName : "Unknown Device";
    d.rawProductType = rawProductType;
    d.jailbroken = detect_jailbroken(afcClient);
    d.is_iPhone = safeGet("DeviceClass") == "iPhone";
    d.serialNumber = safeGet("SerialNumber");
    d.mobileEquipmentIdentifier = safeGet("MobileEquipmentIdentifier");

    /*BatteryInfo*/
    plist_t diagnostics = nullptr;
    get_battery_info(diagRelay, diagnostics);

    if (!diagnostics) {
        qDebug() << "Failed to get diagnostics plist.";
        return d;
    }
    try {
        PlistNavigator ioreg = PlistNavigator(diagnostics);

        // old devices do not have "BatteryData"
        d.oldDevice = !ioreg["BatteryData"];
        if (d.oldDevice) {
            parseOldDevice(ioreg, d);
            plist_free(diagnostics);
            diagnostics = nullptr;
            return d;
        }

        bool newerThaniPhone8 =
            is_product_type_newer(rawProductType, std::string("iPhone8,1"));

        uint64_t cycleCount = ioreg["BatteryData"]["CycleCount"].getUInt();

        // Battery serial number
        std::string batterySerialNumber =
            ioreg["BatteryData"]["BatterySerialNumber"].getString();

        uint64_t designCapacity =
            ioreg["BatteryData"]["DesignCapacity"].getUInt();

        uint64_t maxCapacity =
            d.is_iPhone ? newerThaniPhone8
                              ? ioreg["AppleRawMaxCapacity"].getUInt()
                              : ioreg["BatteryData"]["MaxCapacity"].getUInt()
                        : ioreg["BatteryData"]["MaxCapacity"].getUInt();

        qDebug() << "Design capacity: " << designCapacity;
        qDebug() << "Max capacity: " << maxCapacity;

        // seems to be to the most accurate way to get health
        d.batteryInfo.health =
            QString::number(
                qBound<int>(0, (maxCapacity * 100) / designCapacity, 100)) +
            "%";
        d.batteryInfo.cycleCount = cycleCount;
        d.batteryInfo.serialNumber = !batterySerialNumber.empty()
                                         ? batterySerialNumber
                                         : "Error retrieving serial number";
        qDebug() << "Cycle count: " << cycleCount;
        parseDeviceBattery(ioreg, d);
        plist_free(diagnostics);
        diagnostics = nullptr;

        return d;
    } catch (const std::exception &e) {
        qDebug() << "Error occurred: " << e.what();
        return d;
    }
}

iDescriptorInitDeviceResult
init_idescriptor_device(const QString &udid,
                        const WirelessInitArgs &wirelessArgs)
{
    const bool isWireless =
        !wirelessArgs.ip.isEmpty() && !wirelessArgs.pairing_file.isEmpty();
    qDebug() << "Initializing iDescriptor device with UDID: " << udid
             << (isWireless ? "over wireless" : "over USB");

    iDescriptorInitDeviceResult result = {};

    UsbmuxdConnectionHandle *usbmuxd_conn = nullptr;
    UsbmuxdAddrHandle *addr_handle = nullptr;
    IdeviceProviderHandle *provider = nullptr;
    LockdowndClientHandle *lockdown = nullptr;
    IdeviceSocketHandle *socket = nullptr;
    AfcClientHandle *afc_client = nullptr;
    AfcClientHandle *afc2_client = nullptr;
    pugi::xml_document infoXml;
    uint32_t actual_device_id = 0;
    IdevicePairingFile *pairing_file = nullptr;
    IdeviceHandle *deviceHandle = nullptr;
    HeartbeatClientHandle *heartbeat = nullptr;
    HeartbeatThread *heartbeatThread = nullptr;
    ImageMounterHandle *image_mounter = nullptr;
    DiagnosticsRelayClientHandle *diagnostics_relay = nullptr;
    LocationSimulationHandle *location_simulation = nullptr;
    plist_t val = nullptr;

    IdeviceFfiError *err =
        idevice_usbmuxd_new_default_connection(0, &usbmuxd_conn);
    if (err) {
        if (!isWireless) {
            qDebug() << "Failed to connect to usbmuxd";
            goto cleanup;
        }
    }

    err = idevice_usbmuxd_default_addr_new(&addr_handle);
    if (err) {
        qDebug() << "Failed to create address handle";
        goto cleanup;
    }

    if (isWireless) {
        // Create IPv4 sockaddr
        struct sockaddr_in addr_in;
        memset(&addr_in, 0, sizeof(addr_in));
        addr_in.sin_family = AF_INET;
        addr_in.sin_port = htons(0);
        inet_pton(AF_INET, wirelessArgs.ip.toUtf8().constData(),
                  &addr_in.sin_addr);
        err = idevice_pairing_file_read(
            wirelessArgs.pairing_file.toUtf8().constData(), &pairing_file);
        if (err) {
            qDebug() << "Failed to read pairing file";
            goto cleanup;
        }

        err = idevice_tcp_provider_new(
            (const idevice_sockaddr *)&addr_in,
            const_cast<IdevicePairingFile *>(pairing_file), APP_LABEL,
            &provider);
        if (err) {
            qDebug() << "Failed to create wireless provider";
            goto cleanup;
        }
        err = heartbeat_connect(provider, &heartbeat);
        if (err) {
            qDebug() << "Failed to start Heartbeat service";
            goto cleanup;
        }
        // udid is mac address here for wireless
        heartbeatThread = new HeartbeatThread(heartbeat, udid);
        heartbeatThread->start();

        while (!heartbeatThread->initialCompleted()) {
            sleep(1);
        }

    } else {

        UsbmuxdDeviceHandle **devices;
        int device_count;
        err =
            idevice_usbmuxd_get_devices(usbmuxd_conn, &devices, &device_count);

        // Find by UDID and get its device_id
        for (size_t i = 0; i < device_count; i++) {
            const char *device_udid =
                idevice_usbmuxd_device_get_udid(devices[i]);
            if (strcmp(device_udid, udid.toUtf8().constData()) == 0) {
                actual_device_id =
                    idevice_usbmuxd_device_get_device_id(devices[i]);
                break;
            }
        }

        // 3. Create provider for the device (actual function name)
        err = usbmuxd_provider_new(addr_handle, 0, udid.toUtf8().constData(),
                                   actual_device_id, APP_LABEL, &provider);
    }

    if (err) {
        qDebug() << "Failed to create provider";
        goto cleanup;
    }

    err = lockdownd_connect(provider, &lockdown);
    if (err) {
        qDebug() << "Failed to connect to lockdown";
        goto cleanup;
    }

    err = idevice_provider_get_pairing_file(provider, &pairing_file);
    if (err) {
        qDebug() << "Failed to get pairing file";
        goto cleanup;
    }

    err = lockdownd_start_session(lockdown, pairing_file);
    if (err) {
        qDebug() << "Failed to start lockdown session";
        goto cleanup;
    }

    if (err) {
        qDebug() << "Failed to connect to Heartbeat client";
        goto cleanup;
    }

    err = afc_client_connect(provider, &afc_client);
    if (err) {
        qDebug() << "Failed to create AFC client";
        goto cleanup;
    }

    err = image_mounter_connect(provider, &image_mounter);
    if (err) {
        qDebug() << "Failed to create Image Mounter client";
        goto cleanup;
    }

    err = diagnostics_relay_client_connect(provider, &diagnostics_relay);

    if (err) {
        qDebug() << "Failed to create Diagnostics Relay client";
        goto cleanup;
    }

    err = afc2_client_connect(provider, &afc2_client);
    if (err) {
        qDebug() << "Failed to create AFC2 client";
        // dont cleanup here, afc2 is optional
    }

    // FIXME: will probably not work on iOS 17 and above
    // requires dev image disk
    // err = location_simulation_connect(provider, &location_simulation);
    // if (err) {
    //     qDebug() << "Failed to create Location Simulation client";
    //     goto cleanup;
    // }
    get_device_info_xml(udid.toUtf8().constData(), lockdown, infoXml);

    lockdownd_get_value(lockdown, "EnableWifiConnections",
                        "com.apple.mobile.wireless_lockdown", &val);
    if (val)
        plist_print(val);

    result.provider = provider;
    result.success = true;
    result.afcClient = afc_client;
    result.afc2Client = afc2_client;
    result.lockdown = lockdown;
    // TODO:remove, not really required to get some stuff going so it can be
    // optional
    result.imageMounter = image_mounter;
    result.diagRelay = std::make_shared<DiagnosticsRelay>(
        DiagnosticsRelay::adopt(diagnostics_relay));
    result.locationSimulation = location_simulation;
    // TODO cache pairing file path
    result.deviceInfo.isWireless = isWireless;
    fullDeviceInfo(infoXml, afc_client, result.diagRelay.get(), result);
    ::QObject::connect(heartbeatThread, &HeartbeatThread::heartbeatFailed,
                       AppContext::sharedInstance(),
                       &AppContext::heartbeatFailed);
    ::QObject::connect(heartbeatThread, &HeartbeatThread::heartbeatThreadExited,
                       AppContext::sharedInstance(), &AppContext::removeDevice);
cleanup:
    // Cleanup on error
    // FIXME: implement proper cleanup
    // one of them causes a crash here, needs investigation
    if (!result.success) {
        qDebug() << "Initialization failed, cleaning up resources."
                 << err->message;
        // if (afc2_client)
        //     afc_client_free(afc2_client);
        // if (afc_client)
        //     afc_client_free(afc_client);
        // if (lockdown)
        //     lockdownd_client_free(lockdown);
        // if (provider)
        //     idevice_provider_free(provider);
        // if (addr_handle)
        //     idevice_usbmuxd_addr_free(addr_handle);
        // if (usbmuxd_conn)
        //     idevice_usbmuxd_connection_free(usbmuxd_conn);
    }

    return result;
}

// #ifdef ENABLE_RECOVERY_DEVICE_SUPPORT
// iDescriptorInitDeviceResultRecovery
// init_idescriptor_recovery_device(uint64_t ecid)
// {
//     qDebug() << "Initializing iDescriptor recovery device with ECID: " <<
//     ecid; iDescriptorInitDeviceResultRecovery result = {};

//     irecv_client_t client = nullptr;
//     const irecv_device_info *deviceInfo = nullptr;
//     irecv_device_t device = nullptr;
//     const DeviceDatabaseInfo *info = nullptr;

//     irecv_error_t ret = irecv_open_with_ecid_and_attempts(
//         &client, ecid, RECOVERY_CLIENT_CONNECTION_TRIES);

//     if (ret != IRECV_E_SUCCESS) {
//         qDebug() << "Failed to open recovery client with ECID:" << ecid
//                  << "Error:" << ret;
//         result.error = ret;
//         goto cleanup;
//     }

//     ret = irecv_get_mode(client, (int *)&result.mode);
//     if (ret != IRECV_E_SUCCESS) {
//         qDebug() << "Failed to get recovery mode. Error:" << ret;
//         result.error = ret;
//         goto cleanup;
//     }

//     deviceInfo = irecv_get_device_info(client);
//     if (!deviceInfo) {
//         qDebug() << "Failed to get device info from recovery client";
//         result.error = IRECV_E_UNKNOWN_ERROR;
//         goto cleanup;
//     }

//     if (irecv_devices_get_device_by_client(client, &device) ==
//             IRECV_E_SUCCESS &&
//         device && device->hardware_model) {
//         qDebug() << "Recovery device hardware_model: "
//                  << device->hardware_model;
//         info =
//             DeviceDatabase::findByHwModel(std::string(device->hardware_model));
//     } else {
//         qDebug() << "Could not resolve hardware_model from client.";
//     }

//     result.displayName =
//         info ? (info->displayName ? info->displayName : info->marketingName)
//              : "Unknown Device";
//     result.deviceInfo = *deviceInfo;
//     result.success = true;

// cleanup:
//     if (client) {
//         irecv_close(client);
//     }

//     return result;
// }
// #endif // ENABLE_RECOVERY_DEVICE_SUPPORT