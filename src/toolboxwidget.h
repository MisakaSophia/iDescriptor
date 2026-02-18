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

#ifndef TOOLBOXWIDGET_H
#define TOOLBOXWIDGET_H

#include "airplaywidget.h"
#include "devdiskimageswidget.h"
#include "devicesidebarwidget.h"
#include "iDescriptor-ui.h"
#include "iDescriptor.h"
#include "networkdeviceswidget.h"
#include "wirelessgalleryimportwidget.h"
#include <QComboBox>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QScrollArea>
#include <QVBoxLayout>
#include <QWidget>
#ifndef __APPLE__
#include "ifusewidget.h"
#endif

class ToolboxWidget : public QWidget
{
    Q_OBJECT
public:
    explicit ToolboxWidget(QWidget *parent = nullptr);
    static void restartDevice(iDescriptorDevice *device);
    static void shutdownDevice(iDescriptorDevice *device);
    static void _enterRecoveryMode(iDescriptorDevice *device);
    static ToolboxWidget *sharedInstance();
    void restartAirPlayWidget();
private slots:
    void onDeviceSelectionChanged();
    void onToolboxClicked(iDescriptorTool tool, bool requiresDevice);
    void onCurrentDeviceChanged(const DeviceSelection &selection);

private:
    void setupUI();
    void updateDeviceList();
    void updateToolboxStates();
    void updateUI();
    ClickableWidget *createToolbox(iDescriptorTool tool,
                                   const QString &description,
                                   bool requiresDevice);
    QComboBox *m_deviceCombo;
    QLabel *m_deviceLabel;
    QScrollArea *m_scrollArea;
    QWidget *m_contentWidget;
    QGridLayout *m_gridLayout;
    QList<QWidget *> m_toolboxes;
    std::string m_uuid;
    DevDiskImagesWidget *m_devDiskImagesWidget = nullptr;
    NetworkDevicesWidget *m_networkDevicesWidget = nullptr;
    AirPlayWidget *m_airplayWidget = nullptr;
#ifndef __APPLE__
    iFuseWidget *m_ifuseWidget = nullptr;
#endif
    WirelessGalleryImportWidget *m_wirelessGalleryImportWidget = nullptr;

signals:
};

#endif // TOOLBOXWIDGET_H
