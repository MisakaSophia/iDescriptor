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

#pragma once
#include <QDebug>
#include <QImage>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QRegularExpression>
#include <QThread>
#include <QtCore/QObject>

// #include "idevice.h"
#include <idevice++/bindings.hpp>
#include <idevice++/core_device_proxy.hpp>
#include <idevice++/diagnostics_relay.hpp>
#include <idevice++/dvt/remote_server.hpp>
#include <idevice++/dvt/screenshot.hpp>
#include <idevice++/ffi.hpp>
#include <idevice++/heartbeat.hpp>
#include <idevice++/installation_proxy.hpp>
#include <idevice++/lockdown.hpp>
#include <idevice++/provider.hpp>
#include <idevice++/readwrite.hpp>
#include <idevice++/rsd.hpp>
#include <idevice++/usbmuxd.hpp>

#include <mutex>
#include <pugixml.hpp>
#include <string>
#include <unordered_map>
#include <vector>

#define TOOL_NAME "iDescriptor"
#define APP_LABEL "iDescriptor"
#define APP_COPYRIGHT                                                          \
    "© 2025 The iDescriptor Project contributors. See AUTHORS for details."
#define AFC2_SERVICE_NAME "com.apple.afc2"
#define RECOVERY_CLIENT_CONNECTION_TRIES 3
#define APPLE_VENDOR_ID 0x05ac
#define REPO_URL "https://github.com/iDescriptor/iDescriptor"
#define SPONSORS_JSON_URL                                                      \
    "https://raw.githubusercontent.com/iDescriptor/iDescriptor/refs/heads/"    \
    "main/sponsors.json"
#define DEVELOPER_DISK_IMAGE_JSON_URL                                          \
    "https://raw.githubusercontent.com/iDescriptor/iDescriptor/refs/heads/"    \
    "main/DeveloperDiskImages.json"

// This is because afc_list_directory accepts  "/var/mobile/Media" as "/"
#define POSSIBLE_ROOT "../../../../"
#define IDEVICE_DEVICE_VERSION(maj, min, patch)                                \
    ((((maj) & 0xFF) << 16) | (((min) & 0xFF) << 8) | ((patch) & 0xFF))
#include "devicemonitor.h"
#include "iDescriptor-utils.h"

#define DeviceLockedMountErrorCode -21
#define NotFoundErrorCode -14
#define ServiceNotFoundErrorCode -15
#define DISK_IMAGE_TYPE_DEVELOPER "Developer"

#define HEARTBEAT_RETRY_LIMIT 2

#define DONATE_URL "https://opencollective.com/idescriptor"

#ifdef __linux__
#define LOCKDOWN_PATH "/var/lib/lockdown"
#elif __APPLE__
#define LOCKDOWN_PATH "/var/db/lockdown"
#else
/* Windows */
#define LOCKDOWN_PATH qgetenv("PROGRAMDATA") + "/Apple/Lockdown"
#endif

struct BatteryInfo {
    QString health;
    uint64_t cycleCount;
    // uint64_t designCapacity;
    // uint64_t maxCapacity;
    // uint64_t fullChargeCapacity;
    std::string serialNumber;
    bool isCharging;
    bool fullyCharged;
    uint64_t currentBatteryLevel;
    enum class ConnectionType {
        USB,
        USB_TYPEC,
    } usbConnectionType;
    uint64_t adapterVoltage; // in mV
    uint64_t watts;
};

//! IOS 12
/* {
"AmountDataAvailable": 6663077888,
"AmountDataReserved": 209715200,
"AmountRestoreAvailable": 11524079616,
"CalculateDiskUsage": "OkilyDokily",
"NANDInfo": <01000000 01000000 01000000 00000080 ... 00 00000000 000000>,
"TotalDataAvailable": 6872793088,
"TotalDataCapacity": 11306721280,
"TotalDiskCapacity": 16000000000,
"TotalSystemAvailable": 0,
"TotalSystemCapacity": 4693204992
}*/
struct DiskInfo {
    uint64_t totalDiskCapacity;
    uint64_t totalDataCapacity;
    uint64_t totalSystemCapacity;
    uint64_t totalDataAvailable;
};

struct DeviceVersion {
    unsigned int major;
    unsigned int minor;
    unsigned int patch;
};

// Carefull not all the vars are initialized in init_device.cpp
struct DeviceInfo {
    enum class ActivationState {
        Activated,
        FactoryActivated,
        Unactivated
    } activationState;
    std::string activationStateAcknowledged;
    std::string productType;
    std::string rawProductType;
    bool jailbroken;
    std::string serialNumber;
    std::string basebandActivationTicketVersion;
    std::string basebandCertId;
    std::string basebandChipID;
    std::string basebandKeyHashInformation;
    std::string aKeyStatus;
    std::string sKeyHash;
    std::string sKeyStatus;
    std::string basebandMasterKeyHash;
    std::string basebandRegionSKU;
    std::string basebandSerialNumber;
    std::string basebandStatus;
    std::string basebandVersion;
    std::string bluetoothAddress;
    std::string boardId;
    std::string productVersion;
    bool brickState;
    std::string buildVersion;
    std::string cpuArchitecture;
    std::string carrierBundleInfoArray_1;
    std::string cfBundleIdentifier;
    std::string cfBundleVersion;
    std::string gid1;
    std::string gid2;
    std::string integratedCircuitCardIdentity;
    std::string internationalMobileSubscriberIdentity;
    std::string mcc;
    std::string mnc;
    std::string mobileEquipmentIdentifier;
    std::string simGid1;
    std::string simGid2;
    std::string slot;
    std::string kCTPostponementInfoAvailable;
    std::string certID;
    std::string chipID;
    std::string chipSerialNo;
    std::string deviceClass;
    std::string deviceColor;
    std::string deviceName;
    std::string dieID;
    std::string ethernetAddress;
    std::string firmwareVersion;
    int fusingStatus;
    std::string hardwareModel;
    std::string hardwarePlatform;
    bool hasSiDP;
    bool hostAttached;
    std::string internationalMobileEquipmentIdentity;
    bool internationalMobileSubscriberIdentityOverride;
    std::string mlbSerialNumber;
    std::string mobileSubscriberCountryCode;
    std::string mobileSubscriberNetworkCode;
    std::string modelNumber;
    std::string ioNVRAMSyncNowProperty;
    bool systemAudioVolumeSaved;
    bool autoBoot;
    int backlightLevel;
    bool productionDevice;
    BatteryInfo batteryInfo;
    DiskInfo diskInfo;
    bool is_iPhone;
    bool oldDevice;
    std::string marketingName;
    std::string regionRaw;
    std::string region;
    DeviceVersion parsedDeviceVersion;
    std::string wifiMacAddress;
    bool isWireless = false;
    // empty on USB devices
    std::string ipAddress;
};

struct iDescriptorDevice {
    std::string udid;
    DeviceMonitorThread::IdeviceConnectionType conn_type;
    IdeviceProviderHandle *provider;
    DeviceInfo deviceInfo;
    AfcClientHandle *afcClient;
    // nullptr if the device is not jailbroken or doesn't have AFC2 installed
    AfcClientHandle *afc2Client;
    LockdowndClientHandle *lockdown;
    mutable std::recursive_mutex mutex;
    ImageMounterHandle *imageMounter;
    std::shared_ptr<DiagnosticsRelay> diagRelay;
    LocationSimulationHandle *locationSimulation;
    // nullptr on USB devices
    QThread *heartbeatThread;
};

struct iDescriptorInitDeviceResult {
    bool success = false;
    IdeviceFfiError error;
    IdeviceProviderHandle *provider;
    DeviceInfo deviceInfo;
    AfcClientHandle *afcClient;
    AfcClientHandle *afc2Client;
    LockdowndClientHandle *lockdown;
    ImageMounterHandle *imageMounter;
    std::shared_ptr<DiagnosticsRelay> diagRelay;
    LocationSimulationHandle *locationSimulation;
    QThread *heartbeatThread;
};
// #ifdef ENABLE_RECOVERY_DEVICE_SUPPORT
// struct iDescriptorRecoveryDevice {
//     uint64_t ecid;
//     irecv_mode mode;
//     uint32_t cpid;
//     uint32_t bdid;
//     std::string displayName;
//     std::recursive_mutex *mutex;
// };
// #endif

struct TakeScreenshotResult {
    bool success = false;
    QImage img;
};

// #ifdef ENABLE_RECOVERY_DEVICE_SUPPORT
// struct iDescriptorInitDeviceResultRecovery {
//     irecv_client_t client = nullptr;
//     irecv_device_info deviceInfo;
//     irecv_error_t error;
//     bool success = false;
//     irecv_mode mode = IRECV_K_RECOVERY_MODE_1;
//     const char *displayName = nullptr;
// };

// #endif

void warn(const QString &message, const QString &title = "Warning",
          QWidget *parent = nullptr);

enum class AddType { Regular, Pairing, Wireless, UpgradeToWireless };

class PlistNavigator
{
private:
    plist_t current_node;

public:
    PlistNavigator(plist_t node) : current_node(node) {}

    // dict key access
    PlistNavigator operator[](const char *key)
    {
        if (!current_node || plist_get_node_type(current_node) != PLIST_DICT) {
            return PlistNavigator(nullptr);
        }
        plist_t next = plist_dict_get_item(current_node, key);
        return PlistNavigator(next);
    }

    PlistNavigator operator[](const std::string &key)
    {
        if (!current_node || plist_get_node_type(current_node) != PLIST_DICT) {
            return PlistNavigator(nullptr);
        }
        plist_t next = plist_dict_get_item(current_node, key.c_str());
        return PlistNavigator(next);
    }

    PlistNavigator operator[](const QString &key)
    {
        if (!current_node || plist_get_node_type(current_node) != PLIST_DICT) {
            return PlistNavigator(nullptr);
        }
        plist_t next =
            plist_dict_get_item(current_node, key.toUtf8().constData());
        return PlistNavigator(next);
    }

    // array index access
    PlistNavigator operator[](int index)
    {
        if (!current_node || plist_get_node_type(current_node) != PLIST_ARRAY) {
            return PlistNavigator(nullptr);
        }
        if (index < 0 ||
            index >= static_cast<int>(plist_array_get_size(current_node))) {
            return PlistNavigator(nullptr);
        }
        plist_t next = plist_array_get_item(current_node, index);
        return PlistNavigator(next);
    }

    operator plist_t() const { return current_node; }
    bool valid() const { return current_node != nullptr; }

    bool getBool() const
    {
        if (!current_node)
            return false;
        uint8_t value = false;
        plist_get_bool_val(current_node, &value);
        return value;
    }

    uint64_t getUInt() const
    {
        if (!current_node)
            return 0;
        uint64_t value = 0;
        plist_get_uint_val(current_node, &value);
        return value;
    }

    std::string getString() const
    {
        if (!current_node)
            return "";
        char *value = nullptr;
        plist_get_string_val(current_node, &value);
        std::string result = value ? value : "";
        if (value)
            free(value);
        return result;
    }
    plist_t getNode() const { return current_node; }

    QVariant toQVariant() const
    {
        if (!current_node) {
            return QVariant();
        }
        // TODO: free
        plist_type type = plist_get_node_type(current_node);
        switch (type) {
        case PLIST_BOOLEAN: {
            uint8_t val;
            plist_get_bool_val(current_node, &val);
            return QVariant(static_cast<bool>(val));
        }
        case PLIST_UINT: {
            uint64_t val;
            plist_get_uint_val(current_node, &val);
            return QVariant(static_cast<qulonglong>(val));
        }
        case PLIST_STRING: {
            char *val = nullptr;
            plist_get_string_val(current_node, &val);
            QString str_val = QString::fromUtf8(val);
            if (val)
                free(val);
            return QVariant(str_val);
        }
        case PLIST_REAL: {
            double val;
            plist_get_real_val(current_node, &val);
            return QVariant(val);
        }
        case PLIST_DICT:
        case PLIST_ARRAY:
        case PLIST_DATA:
        case PLIST_DATE:
        default: {
            return QVariant("Unsupported Type");
        }
        }
    }
};

// afc_error_t safe_afc_read_directory(afc_client_t afcClient, idevice_t device,
//                                     const char *path, char ***dirs);

std::string parse_product_type(const std::string &productType);

// #ifdef ENABLE_RECOVERY_DEVICE_SUPPORT
// std::string parse_recovery_mode(irecv_mode productType);
// #endif

struct MediaEntry {
    std::string name;
    bool isDir;
};

struct AFCFileTree {
    std::vector<MediaEntry> entries;
    bool success;
    std::string currentPath;
};

AFCFileTree
get_file_tree(const iDescriptorDevice *device, bool checkDir,
              const std::string &path = "/",
              std::optional<AfcClientHandle *> altAfc = std::nullopt);

bool detect_jailbroken(AfcClientHandle *afc);

void get_device_info_xml(const char *udid, LockdowndClientHandle *client,
                         pugi::xml_document &infoXml);

struct WirelessInitArgs {
    const QString ip;
    const QString pairing_file;
};
iDescriptorInitDeviceResult
init_idescriptor_device(const QString &udid,
                        const WirelessInitArgs &wirelessArgs = {"", ""});

// #ifdef ENABLE_RECOVERY_DEVICE_SUPPORT
// iDescriptorInitDeviceResultRecovery
// init_idescriptor_recovery_device(uint64_t ecid);
// #endif
// bool set_location(idevice_t device, char *lat, char *lon);

// bool shutdown(idevice_t device);

IdeviceFfiError *mount_dev_image(const iDescriptorDevice *device,
                                 const char *image_file,
                                 const char *signature_file);

struct MountedImageInfo {
    IdeviceFfiError *err;
    uint8_t *signature;
    size_t signature_len;
};

struct MountedImageResult {
    bool success;
    IdeviceFfiError *err;
};

MountedImageInfo _get_mounted_image(const iDescriptorDevice *device);

void mounted_image_info_free(MountedImageInfo &info);

// bool restart(std::string udid);

enum class ImageCompatibility {
    Compatible,      // Exact match or known compatible version
    MaybeCompatible, // Major version matches but minor doesn't
    NotCompatible    // Not compatible
};

struct ImageInfo {
    QString version;
    QString dmgPath;
    QString sigPath;
    ImageCompatibility compatibility = ImageCompatibility::NotCompatible;
    bool isDownloaded = false;
    bool isMounted = false;
};

// std::string safeGetXML(const char *key, pugi::xml_node dict);

void get_battery_info(DiagnosticsRelay *diagRelay, plist_t &diagnostics);

// void parseOldDeviceBattery(PlistNavigator &ioreg, DeviceInfo &d);
// void parseDeviceBattery(PlistNavigator &ioreg, DeviceInfo &d);

void fetchAppIconFromApple(
    QNetworkAccessManager *manager, const QString &bundleId,
    std::function<void(const QPixmap &, const QJsonObject &)> callback);

void _get_cable_info(const iDescriptorDevice *device, plist_t &response);

struct NetworkDevice {
    QString name;                           // service name
    QString hostname;                       // e.g., iPhone-2.local
    QString address;                        // IPv4 or IPv6 address
    uint16_t port = 22;                     // SSH port
    std::map<std::string, std::string> txt; // TXT records
    QString macAddress;                     // MAC address if available
    bool operator==(const NetworkDevice &other) const
    {
        return name == other.name && address == other.address;
    }
};

QPixmap load_heic(const QByteArray &data);

QByteArray read_afc_file_to_byte_array(const iDescriptorDevice *device,
                                       const char *path);

bool isDarkMode();

IdeviceFfiError *_install_IPA(const iDescriptorDevice *device,
                              const char *filePath, const char *ipaName);

// Helper struct for semantic version comparison
struct AppVersion {
    int major = 0;
    int minor = 0;
    int patch = 0;

    static AppVersion fromString(QString versionString)
    {
        // Keep only digits and dots for comparison
        versionString.remove(QRegularExpression("[^\\d.]"));
        AppVersion v;
        QStringList parts = versionString.split('.');
        if (parts.size() > 0)
            v.major = parts[0].toInt();
        if (parts.size() > 1)
            v.minor = parts[1].toInt();
        if (parts.size() > 2)
            v.patch = parts[2].toInt();
        return v;
    }

    bool operator<(const AppVersion &other) const
    {
        if (major != other.major)
            return major < other.major;
        if (minor != other.minor)
            return minor < other.minor;
        return patch < other.patch;
    }

    bool operator==(const AppVersion &other) const
    {
        return major == other.major && minor == other.minor &&
               patch == other.patch;
    }

    bool operator>(const AppVersion &other) const
    {
        return !(*this < other || *this == other);
    }
    bool operator<=(const AppVersion &other) const
    {
        return (*this < other || *this == other);
    }
    bool operator>=(const AppVersion &other) const { return !(*this < other); }
};

// Checks if the current app version matches a given version condition
inline bool versionMatches(const QString &currentVersionStr,
                           const QString &conditionStr)
{
    AppVersion currentVersion = AppVersion::fromString(currentVersionStr);
    AppVersion conditionVersion = AppVersion::fromString(conditionStr);

    if (conditionStr.startsWith("<="))
        return currentVersion <= conditionVersion;
    if (conditionStr.startsWith(">="))
        return currentVersion >= conditionVersion;
    if (conditionStr.startsWith("<"))
        return currentVersion < conditionVersion;
    if (conditionStr.startsWith(">"))
        return currentVersion > conditionVersion;

    // Exact match
    return currentVersion == conditionVersion;
}

inline QJsonObject getVersionedConfig(const QJsonObject &rootObj)
{
    QStringList keys = rootObj.keys();
    for (const QString &key : keys) {
        if (versionMatches(APP_VERSION, key)) {
            qDebug() << "getVersionedConfig picked version:" << key;
            return rootObj[key].toObject();
        }
    }
    return QJsonObject();
}

inline void free_directory_listing(char **entries, size_t count)
{
    if (!entries)
        return;
    for (size_t i = 0; i < count; i++) {
        if (entries[i]) {
            free(entries[i]);
        }
    }
    free(entries);
}

inline int read_file(const char *filename, uint8_t **data, size_t *length)
{
    FILE *file = fopen(filename, "rb");
    if (!file) {
        perror("Failed to open file");
        return 0;
    }

    fseek(file, 0, SEEK_END);
    *length = ftell(file);
    fseek(file, 0, SEEK_SET);

    *data = (uint8_t *)malloc(*length);
    if (!*data) {
        perror("Failed to allocate memory");
        fclose(file);
        return 0;
    }

    if (fread(*data, 1, *length, file) != *length) {
        perror("Failed to read file");
        free(*data);
        fclose(file);
        return 0;
    }

    fclose(file);
    return 1;
}

struct ExportItem {
    QString sourcePathOnDevice;
    QString suggestedFileName;
    int itemIndex = -1;

    ExportItem() = default;
    ExportItem(const QString &sourcePath, const QString &fileName, int index)
        : sourcePathOnDevice(sourcePath), suggestedFileName(fileName),
          itemIndex(index)
    {
    }
};

struct ExportResult {
    QString sourceFilePath;
    QString outputFilePath;
    bool success = false;
    QString errorMessage;
    qint64 bytesTransferred = 0;
};

struct ExportJobSummary {
    QUuid jobId;
    int totalItems = 0;
    int successfulItems = 0;
    int failedItems = 0;
    qint64 totalBytesTransferred = 0;
    QString destinationPath;
    bool wasCancelled = false;
};

struct ExportJob {
    QUuid jobId;
    iDescriptorDevice *device = nullptr;
    QList<ExportItem> items;
    QString destinationPath;
    std::optional<AfcClientHandle *> altAfc;
    std::atomic<bool> cancelRequested{false};
    QUuid statusBalloonProcessId;
};