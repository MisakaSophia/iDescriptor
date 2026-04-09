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

#include "../../appcontext.h"
#include "../../devicedatabase.h"
#include "../../iDescriptor.h"
#include <QDebug>

#ifdef _WIN32
#include "../../platform/windows/win_common.h"
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

#include <sstream>
#include <string.h>
#include <string>

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

void parseOldDeviceBattery(XmlPlistDict &ioreg, DeviceInfo &d)
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

void parseOldDevice(XmlPlistDict &ioreg, DeviceInfo &d)
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

void parseDeviceBattery(XmlPlistDict &ioreg, DeviceInfo &d)
{
    d.batteryInfo.isCharging = ioreg["IsCharging"].getBool();

    d.batteryInfo.fullyCharged = ioreg["FullyCharged"].getBool();

    /* data is sometimes not accurate here so we need to calculate */
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

void fullDeviceInfo(const pugi::xml_document &doc, DeviceInfo &d)
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
    d.UniqueDeviceID = safeGet("UniqueDeviceID");

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
            XmlPlistDict root(dict);
            pugi::xml_node afc_info_node = root["AFC_INFO"].getNode();
            if (afc_info_node) {
                // FIXME: could be handled better
                auto safeGetAfcString = [&](const char *key) -> std::string {
                    for (pugi::xml_node child = afc_info_node.first_child();
                         child; child = child.next_sibling()) {
                        if (strcmp(child.name(), "key") == 0 &&
                            strcmp(child.text().as_string(), key) == 0) {
                            pugi::xml_node value = child.next_sibling();
                            if (value) {
                                return value.text().as_string();
                            }
                        }
                    }
                    return "";
                };

                // Lambda to parse uint64_t values from the afc_info_node
                auto safeParseAfcU64 = [&](const char *key) -> uint64_t {
                    std::string s = safeGetAfcString(key);
                    if (s.empty())
                        return 0;
                    try {
                        return std::stoull(s);
                    } catch (...) {
                        qDebug()
                            << "Failed to parse AFC_INFO key to uint64_t:"
                            << key << "value:" << QString::fromStdString(s);
                        return 0;
                    }
                };

                d.diskInfo.totalDataAvailable = safeParseAfcU64("FreeBytes");
            }
        } catch (const std::exception &e) {
            qDebug() << "Error parsing disk info: " << e.what();
        }
    } catch (const std::exception &e) {
        qDebug() << e.what();
        /*It's ok if any of those fails*/
    }

    std::string _activationState = safeGet("ActivationState");

    /* older devices dont have fusing status lets default to ProductionSOC
    for
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
    d.jailbroken = safeGetBool("Jailbroken");
    d.is_iPhone = safeGet("DeviceClass") == "iPhone";
    d.serialNumber = safeGet("SerialNumber");
    d.mobileEquipmentIdentifier = safeGet("MobileEquipmentIdentifier");
    d.isWireless = safeGet("ConnectionType") == "Wireless";

    /*BatteryInfo*/
    XmlPlistDict ioreg =
        XmlPlistDict(doc.child("plist").child("dict"))["DIAG_INFO"];

    if (!ioreg.valid()) {
        qDebug() << "Failed to get diagnostics plist.";
        return;
    }

    try {
        // old devices do not have "BatteryData"
        d.oldDevice = ioreg["BatteryData"].valid() ? false : true;
        if (d.oldDevice) {
            parseOldDevice(ioreg, d);
            return;
        }

        bool newerThaniPhone8 =
            iDescriptor::Utils::isProductTypeNewer(rawProductType, "iPhone8,1");

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

        return;
    } catch (const std::exception &e) {
        qDebug() << "Error occurred: " << e.what();
        return;
    }
}

#ifdef ENABLE_RECOVERY_DEVICE_SUPPORT
void init_idescriptor_recovery_device(
    uint64_t ecid, iDescriptorInitDeviceResultRecovery &result)
{
    qDebug() << "Initializing iDescriptor recovery device with ECID: " << ecid;
    result = {};

    irecv_client_t client = nullptr;
    const irecv_device_info *deviceInfo = nullptr;
    irecv_device_t device = nullptr;
    const DeviceDatabaseInfo *info = nullptr;

    irecv_error_t ret = irecv_open_with_ecid_and_attempts(
        &client, ecid, RECOVERY_CLIENT_CONNECTION_TRIES);

    if (ret != IRECV_E_SUCCESS) {
        qDebug() << "Failed to open recovery client with ECID:" << ecid
                 << "Error:" << ret;
        result.error = ret;
        goto cleanup;
    }

    ret = irecv_get_mode(client, (int *)&result.mode);
    if (ret != IRECV_E_SUCCESS) {
        qDebug() << "Failed to get recovery mode. Error:" << ret;
        result.error = ret;
        goto cleanup;
    }

    deviceInfo = irecv_get_device_info(client);
    if (!deviceInfo) {
        qDebug() << "Failed to get device info from recovery client";
        result.error = IRECV_E_UNKNOWN_ERROR;
        goto cleanup;
    }

    if (irecv_devices_get_device_by_client(client, &device) ==
            IRECV_E_SUCCESS &&
        device && device->hardware_model) {
        qDebug() << "Recovery device hardware_model: "
                 << device->hardware_model;
        info =
            DeviceDatabase::findByHwModel(std::string(device->hardware_model));
    } else {
        qDebug() << "Could not resolve hardware_model from client.";
    }

    result.displayName =
        info ? (info->displayName ? info->displayName : info->marketingName)
             : "Unknown Device";
    result.deviceInfo = *deviceInfo;
    result.success = true;

cleanup:
    if (client) {
        irecv_close(client);
    }
}
#endif // ENABLE_RECOVERY_DEVICE_SUPPORT
