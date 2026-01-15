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

#include "../../iDescriptor.h"
#include "../../servicemanager.h"
#include <QDebug>
#include <iostream>
#include <string.h>

AFCFileTree get_file_tree(const iDescriptorDevice *device, bool checkDir,
                          const std::string &path,
                          std::optional<AfcClientHandle *> altAfc)
{
    qDebug() << "Getting file tree for path:" << QString::fromStdString(path);
    AFCFileTree result;
    result.currentPath = path;
    result.success = false;

    char **dirs = nullptr;
    size_t count = 0;

    // Use safe wrapper to read directory
    IdeviceFfiError *err = ServiceManager::safeAfcReadDirectory(
        device, path.c_str(), &dirs, count, altAfc);

    if (err) {
        qDebug() << "Failed to read directory:" << path.c_str()
                 << "Error:" << err->message << "Code:" << err->code;
        idevice_error_free(err);
        return result;
    }

    if (!dirs) {
        result.success = true;
        return result;
    }

    // Iterate through directory entries
    for (int i = 0; dirs[i]; i++) {
        // qDebug() << "Found entry:" << dirs[i];
        std::string entryName = dirs[i];
        if (entryName == "." || entryName == "..")
            continue;

        std::string fullPath = path;
        if (fullPath.back() != '/')
            fullPath += "/";
        fullPath += entryName;

        if (!checkDir) {
            result.entries.push_back({entryName, false});
            continue;
        }

        // Get file info using safe wrapper
        AfcFileInfo info = {};
        IdeviceFfiError *info_err = ServiceManager::safeAfcGetFileInfo(
            device, fullPath.c_str(), &info, altAfc);

        if (info_err) {
            qDebug() << "Failed to get file info for:" << fullPath.c_str()
                     << "Error:" << info_err->message
                     << "Code:" << info_err->code;
        }

        bool isDir = false;
        if (!info_err) {
            // qDebug() << "Entry:" << entryName.c_str() << "Type:" <<
            // info.st_ifmt
            //          << "Size:" << info.size;
            if (strcmp(info.st_ifmt, "S_IFDIR") == 0) {
                isDir = true;
            } else if (strcmp(info.st_ifmt, "S_IFLNK") == 0) {
                // Check if symlink points to a directory
                char **dir_contents = nullptr;
                size_t count = 0;
                // FIXME: recursively call safeAfcGetFileInfo to figure out if
                // it's a dir
                IdeviceFfiError *link_err =
                    ServiceManager::safeAfcReadDirectory(
                        device, fullPath.c_str(), &dir_contents, count, altAfc);

                if (!link_err) {
                    isDir = true;
                    free_directory_listing(dir_contents, count);
                }

                if (link_err) {
                    idevice_error_free(link_err);
                }
            }

            afc_file_info_free(&info);
        }

        if (info_err) {
            idevice_error_free(info_err);
        }

        result.entries.push_back({entryName, isDir});
    }

    // Free the directory list
    if (dirs) {
        free_directory_listing(dirs, count);
    }

    result.success = true;
    return result;
}