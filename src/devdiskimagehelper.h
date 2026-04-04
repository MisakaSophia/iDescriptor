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

#ifndef DEVDISKIMAGEHELPER_H
#define DEVDISKIMAGEHELPER_H

#include "iDescriptor-ui.h"
#include "iDescriptor.h"
#include "settingsmanager.h"
#include <QDialog>
#include <QLabel>
#include <QPushButton>
#include <QWidget>

class ZLoadingWidget;

class DevDiskImageHelper : public QDialog
{
    Q_OBJECT
public:
    explicit DevDiskImageHelper(const std::shared_ptr<iDescriptorDevice> device,
                                QWidget *parent = nullptr);

    // Start the mounting process
    void start();
    void mountVersion(const QString &version);

    static bool
    canMountForDevice(const std::shared_ptr<iDescriptorDevice> device)
    {
        /*
            iOS 17 and later are not supported
            even though there are some images called "Personalized Disk Images"
            but we dont support them for now
        */
        return device->ios_version < 17;
    }
signals:
    void mountingCompleted(bool success);
    void downloadStarted();
    void downloadCompleted(bool success);

private slots:
    void checkAndMount();
    void onRetryButtonClicked();

private:
    void setupUI();
    void finishWithSuccess(bool wait = false);
    void handleMounting(const QString &version);

    const std::shared_ptr<iDescriptorDevice> m_device;

    QLabel *m_statusLabel;
    ZLoadingWidget *m_loadingWidget;

    QString m_downloadingVersion;

    // set when called with mountVersion
    QString m_version = QString();
};

#endif // DEVDISKIMAGEHELPER_H
