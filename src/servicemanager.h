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

#ifndef SERVICEMANAGER_H
#define SERVICEMANAGER_H

#include "iDescriptor.h"
#include <QCoreApplication>
#include <QDebug>
#include <QFuture>
#include <functional>
#include <mutex>
#include <optional>

/**
 * @brief Centralized manager for device service operations with thread safety
 *
 * This class provides thread-safe wrappers for all device operations to prevent
 * crashes when devices are unplugged during active operations
 */
class ServiceManager
{
public:
    template <typename T>
    static T
    executeOperation(const iDescriptorDevice *device,
                     std::function<T(AfcClientHandle *)> operation,
                     std::optional<AfcClientHandle *> altAfc = std::nullopt)
    {
        if (QCoreApplication::closingDown() || !device) {
            return T{}; // Return default-constructed value for the type
        }

        std::lock_guard<std::recursive_mutex> lock(device->mutex);

        // Double-check device is still valid after acquiring lock
        if (!device->afcClient) {
            return T{};
        }

        if (altAfc && !*altAfc) {
            // altAfc was explicitly provided but is null, which is an
            // invalid state.
            return T{};
        }

        // Determine which client to use
        AfcClientHandle *client = altAfc ? *altAfc : device->afcClient;

        return operation(client);
    }

    template <typename T>
    static T
    executeOperation(const iDescriptorDevice *device,
                     std::function<T()> operation,
                     std::optional<AfcClientHandle *> altAfc = std::nullopt)
    {
        if (QCoreApplication::closingDown() || !device) {
            qDebug() << "[executeOperation] Device or mutex is null";
            return T{}; // Return default-constructed value for the type
        }

        std::lock_guard<std::recursive_mutex> lock(device->mutex);

        // Double-check device is still valid after acquiring lock
        if (!device->afcClient) {
            qDebug() << "[executeOperation] AFC client is null";
            return T{};
        }

        if (altAfc && !*altAfc) {
            // altAfc was explicitly provided but is null, which is an
            // invalid state.
            qDebug() << "[executeOperation] altAfc is null";
            return T{};
        }
        return operation();
    }

    template <typename T>
    static T
    executeOperation(const iDescriptorDevice *device,
                     std::function<T()> operation, T failureValue,
                     std::optional<AfcClientHandle *> altAfc = std::nullopt)
    {
        if (QCoreApplication::closingDown() || !device) {
            return failureValue;
        }

        std::lock_guard<std::recursive_mutex> lock(device->mutex);

        // Double-check device is still valid after acquiring lock
        if (!device->afcClient) {
            return failureValue;
        }

        if (altAfc && !*altAfc) {
            // altAfc was explicitly provided but is null, which is an
            // invalid state.
            return failureValue;
        }

        return operation();
    }

    static void
    executeOperation(const iDescriptorDevice *device,
                     std::function<void()> operation,
                     std::optional<AfcClientHandle *> altAfc = std::nullopt)
    {
        if (QCoreApplication::closingDown() || !device) {
            return;
        }

        std::lock_guard<std::recursive_mutex> lock(device->mutex);

        // Double-check device is still valid after acquiring lock
        if (!device->afcClient) {
            return;
        }

        if (altAfc && !*altAfc) {
            // altAfc was explicitly provided but is null, which is an
            // invalid state.
            return;
        }

        operation();
    }

    static void
    executeVoidOperation(const iDescriptorDevice *device,
                         std::function<void()> operation,
                         std::optional<AfcClientHandle *> altAfc = std::nullopt)
    {
        if (QCoreApplication::closingDown() || !device) {
            return;
        }

        std::lock_guard<std::recursive_mutex> lock(device->mutex);

        // Double-check device is still valid after acquiring lock
        if (!device->afcClient) {
            return;
        }

        if (altAfc && !*altAfc) {
            // altAfc was explicitly provided but is null, which is an
            // invalid state.
            return;
        }

        operation();
    }

    static IdeviceFfiError *executeAfcOperation(
        const iDescriptorDevice *device,
        std::function<IdeviceFfiError *(AfcFileHandle *handle)> operation,
        AfcFileHandle *handle)
    {
        try {
            if (QCoreApplication::closingDown() || !device) {
                return new IdeviceFfiError{1, "DEVICE_OR_MUTEX_IS_NULL"};
            }

            std::lock_guard<std::recursive_mutex> lock(device->mutex);

            if (!device->afcClient) {
                return new IdeviceFfiError{1, "AFC_CLIENT_IS_NULL"};
            }

            if (!handle) {
                return new IdeviceFfiError{1, "FILE HANDLE IS NULL"};
            }

            return operation(handle);
        } catch (const std::exception &e) {
            qDebug() << "Exception in executeAfcOperation:" << e.what();
            return new IdeviceFfiError{1, "AFC_CLIENT_IS_NULL"};
        }
    }

    static IdeviceFfiError *executeAfcClientOperation(
        const iDescriptorDevice *device,
        std::function<IdeviceFfiError *(AfcClientHandle *client)> operation,
        std::optional<AfcClientHandle *> altAfc = std::nullopt)
    {
        try {
            if (QCoreApplication::closingDown() || !device) {
                qDebug()
                    << "[executeAfcClientOperation] Device or mutex is null";
                return new IdeviceFfiError{1, "DEVICE_OR_MUTEX_IS_NULL"};
            }

            std::lock_guard<std::recursive_mutex> lock(device->mutex);

            if (!device->afcClient) {
                qDebug() << "[executeAfcClientOperation] AFC client is null";
                return new IdeviceFfiError{1, "AFC_CLIENT_IS_NULL"};
            }

            if (altAfc && !*altAfc) {
                // altAfc was explicitly provided but is null, which is an
                // invalid state.
                qDebug() << "[executeAfcClientOperation] altAfc is null";
                // c string is not safe in IdeviceFfiError ?
                return new IdeviceFfiError{1, "ALT_AFC_CLIENT_IS_NULL"};
            }

            // Determine which client to use
            AfcClientHandle *client = altAfc ? *altAfc : device->afcClient;
            return operation(client);
        } catch (const std::exception &e) {
            qDebug() << "Exception in executeAfcOperation:" << e.what();
            return new IdeviceFfiError{1, "AFC_CLIENT_IS_NULL"};
        }
    }

    template <typename T>
    static T executeAfcClientOperation(
        const iDescriptorDevice *device,
        std::function<T(AfcClientHandle *client)> operation,
        std::optional<AfcClientHandle *> altAfc = std::nullopt)
    {
        try {
            if (QCoreApplication::closingDown() || !device) {
                return T{};
            }

            std::lock_guard<std::recursive_mutex> lock(device->mutex);

            // Double-check device is still valid after acquiring lock
            if (!device->afcClient) {
                return T{};
            }

            if (altAfc && !*altAfc) {
                return T{};
            }

            // Determine which client to use
            AfcClientHandle *client = altAfc ? *altAfc : device->afcClient;
            return operation(client);
        } catch (const std::exception &e) {
            qDebug() << "Exception in executeAfcOperation:" << e.what();
            return T{};
        }
    }

    // Specific AFC operation wrappers
    static IdeviceFfiError *safeAfcReadDirectory(
        const iDescriptorDevice *device, const char *path, char ***dirs,
        size_t *count, std::optional<AfcClientHandle *> altAfc = std::nullopt);

    static IdeviceFfiError *
    safeAfcGetFileInfo(const iDescriptorDevice *device, const char *path,
                       AfcFileInfo *info,
                       std::optional<AfcClientHandle *> altAfc = std::nullopt);

    static IdeviceFfiError *
    safeAfcFileOpen(const iDescriptorDevice *device, const char *path,
                    AfcFopenMode mode, AfcFileHandle **handle,
                    std::optional<AfcClientHandle *> altAfc = std::nullopt);

    static IdeviceFfiError *safeAfcFileRead(const iDescriptorDevice *device,
                                            AfcFileHandle *handle,
                                            uint8_t **data, uintptr_t length,
                                            size_t *bytes_read);
    static IdeviceFfiError *safeAfcFileWrite(const iDescriptorDevice *device,
                                             AfcFileHandle *handle,
                                             const uint8_t *data,
                                             uint32_t length);
    static IdeviceFfiError *safeAfcFileClose(const iDescriptorDevice *device,
                                             AfcFileHandle *handle);
    static IdeviceFfiError *safeAfcFileSeek(const iDescriptorDevice *device,
                                            AfcFileHandle *handle,
                                            int64_t offset, int whence);
    static IdeviceFfiError *safeAfcFileTell(const iDescriptorDevice *device,
                                            AfcFileHandle *handle,
                                            int64_t *position);
    // Utility functions
    static QByteArray safeReadAfcFileToByteArray(
        const iDescriptorDevice *device, const char *path,
        std::optional<AfcClientHandle *> altAfc = std::nullopt);
    static AFCFileTree
    safeGetFileTree(const iDescriptorDevice *device, const std::string &path,
                    bool checkDir,
                    std::optional<AfcClientHandle *> altAfc = std::nullopt);
    static QFuture<AFCFileTree>
    getFileTreeAsync(const iDescriptorDevice *device, const std::string &path,
                     bool checkDir,
                     std::optional<AfcClientHandle *> altAfc = std::nullopt);
    static MountedImageInfo getMountedImage(const iDescriptorDevice *device);
    static IdeviceFfiError *mountImage(const iDescriptorDevice *device,
                                       const char *image_file,
                                       const char *signature_file);
    static void getCableInfo(const iDescriptorDevice *device,
                             plist_t &response);

    static IdeviceFfiError *install_IPA(const iDescriptorDevice *device,
                                        const char *filePath,
                                        const char *fileName);
    static bool enableWirelessConnections(const iDescriptorDevice *device);

    // File export operations
    static IdeviceFfiError *exportFileToPath(
        const iDescriptorDevice *device, const char *device_path,
        const char *local_path,
        std::function<void(qint64, qint64)> progressCallback = nullptr,
        std::atomic<bool> *cancelRequested = nullptr,
        std::optional<AfcClientHandle *> altAfc = std::nullopt);

    static IdeviceFfiError *
    importFileToPath(const iDescriptorDevice *device, const char *local_path,
                     const char *device_path,
                     std::function<void(qint64, qint64)> progressCallback,
                     std::atomic<bool> *cancelRequested,
                     std::optional<AfcClientHandle *> altAfc = std::nullopt);

    static IdeviceFfiError *
    deletePath(const iDescriptorDevice *device, const char *path,
               std::optional<AfcClientHandle *> altAfc = std::nullopt);

    static IdeviceFfiError *
    takeScreenshot(const iDescriptorDevice *device,
                   ScreenshotrClientHandle *screenshotrClient,
                   ScreenshotData *screenshot);
    static IdeviceFfiError *
    revealDeveloperModeOptionInUI(const iDescriptorDevice *device);
    static IdeviceFfiError *enableDevMode(const iDescriptorDevice *device);
    static IdeviceFfiError *
    safeParseOldDeviceBattery(const iDescriptorDevice *device,
                              PlistNavigator &ioreg, DeviceInfo &d);
    static IdeviceFfiError *
    safeParseDeviceBattery(const iDescriptorDevice *device,
                           PlistNavigator &ioreg, DeviceInfo &d);
};

#endif // SERVICEMANAGER_H
