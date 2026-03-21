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

#ifndef DEVICEMANAGERWIDGET_H
#define DEVICEMANAGERWIDGET_H

class DeviceMenuWidget;

#include "devicemenuwidget.h"
#include "devicependingwidget.h"
#include "devicesidebarwidget.h"
#include "iDescriptor.h"
#ifdef ENABLE_RECOVERY_DEVICE_SUPPORT
#include "recoverydeviceinfowidget.h"
#endif
#include "appcontext.h"
#include "mainwindow.h"
#include "settingsmanager.h"
#include <QDebug>
#include <QHBoxLayout>
#include <QMap>
#include <QStackedWidget>
#include <QWidget>

class DeviceManagerWidget : public QWidget
{
    Q_OBJECT

public:
    explicit DeviceManagerWidget(QWidget *parent = nullptr);

    void setCurrentDevice(const std::string &uuid);
    std::string getCurrentDevice() const;

signals:
    void updateNoDevicesConnected();

private slots:
    void onDeviceSelectionChanged(const DeviceSelection &selection);

protected:
    void resizeEvent(QResizeEvent *event) override;

private:
    void setupUI();
    void updateUI();
    void addDevice(const iDescriptorDevice *device);
    void removeDevice(const std::string &uuid);
#ifdef ENABLE_RECOVERY_DEVICE_SUPPORT
    void addRecoveryDevice(const iDescriptorRecoveryDevice *device);
    void removeRecoveryDevice(uint64_t ecid);
#endif
    void addPendingDevice(const QString &udid, bool locked);
    void addPairedDevice(const iDescriptorDevice *device);
    void removePendingDevice(const QString &udid);

    QHBoxLayout *m_mainLayout;
    DeviceSidebarWidget *m_sidebar;
    QStackedWidget *m_stackedWidget;

    QMap<std::string, std::pair<DeviceMenuWidget *, DeviceSidebarItem *>>
        m_deviceWidgets; // Map to store devices by UDID

    QMap<std::string,
         std::pair<DevicePendingWidget *, DevicePendingSidebarItem *>>
        m_pendingDeviceWidgets; // Map to store devices by UDID

#ifdef ENABLE_RECOVERY_DEVICE_SUPPORT
    QMap<uint64_t,
         std::pair<RecoveryDeviceInfoWidget *, RecoveryDeviceSidebarItem *>>
        m_recoveryDeviceWidgets; // Map to store recovery devices by ECID
#endif

    std::string m_currentDeviceUuid;
    QLabel *m_noDevicesLabel = nullptr;
};

#endif // DEVICEMANAGERWIDGET_H