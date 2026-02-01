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

#include "toolboxwidget.h"
#include "airplaywindow.h"
#include "appcontext.h"
#include "cableinfowidget.h"
#include "devdiskimageswidget.h"
#include "devdiskmanager.h"
#include "iDescriptor-ui.h"
#include "iDescriptor.h"
#ifndef __APPLE__
#include "ifusewidget.h"
#endif
#include "livescreenwidget.h"
#include "querymobilegestaltwidget.h"
#include "virtuallocationwidget.h"
#include "wirelessgalleryimportwidget.h"
#include <QApplication>
#include <QDebug>
#include <QMessageBox>
#include <QStyle>

struct iDescriptorToolWidget {
    iDescriptorTool tool;
    QString description;
    bool requiresDevice;
    QString iconName;
};

ToolboxWidget::ToolboxWidget(QWidget *parent) : QWidget{parent}
{
    setupUI();
    updateDeviceList();
    updateToolboxStates();

    connect(AppContext::sharedInstance(), &AppContext::deviceChange, this,
            &ToolboxWidget::updateUI);
    connect(AppContext::sharedInstance(),
            &AppContext::currentDeviceSelectionChanged, this,
            &ToolboxWidget::onCurrentDeviceChanged);
}

void ToolboxWidget::setupUI()
{
    QVBoxLayout *mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(0, 0, 0, 0);

    // Device selection section
    QHBoxLayout *deviceLayout = new QHBoxLayout();
    m_deviceLabel = new QLabel("Device:");
    m_deviceCombo = new QComboBox();
    m_deviceCombo->setMinimumWidth(200);

    deviceLayout->addWidget(m_deviceLabel);
    deviceLayout->addWidget(m_deviceCombo);
    deviceLayout->setContentsMargins(15, 5, 15, 5);
    deviceLayout->addStretch();

    mainLayout->addLayout(deviceLayout);

    // Scroll area for toolboxes
    m_scrollArea = new QScrollArea();
    m_scrollArea->setWidgetResizable(true);
    m_scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_scrollArea->setStyleSheet(
        "QScrollArea { background: transparent; border: none; }");
    m_scrollArea->viewport()->setStyleSheet("background: transparent;");

    m_contentWidget = new QWidget();
    QVBoxLayout *contentLayout = new QVBoxLayout(m_contentWidget);
    contentLayout->setSpacing(20);
    contentLayout->setContentsMargins(0, 0, 0, 0);

    // Main Tools Section
    QLabel *mainToolsLabel = new QLabel("Tools");
    mainToolsLabel->setStyleSheet(
        "font-weight: bold; font-size: 14px; margin-left: 10px");
    contentLayout->addWidget(mainToolsLabel);

    QWidget *mainToolsWidget = new QWidget();
    m_gridLayout = new QGridLayout(mainToolsWidget);
    m_gridLayout->setSpacing(10);

    QList<iDescriptorToolWidget> mainToolWidgets;
    mainToolWidgets.append(
        {iDescriptorTool::Airplayer, "Cast your device screen ", false, ""});
    mainToolWidgets.append({iDescriptorTool::VirtualLocation,
                            "Simulate GPS location on your device", true, ""});
    mainToolWidgets.append({iDescriptorTool::LiveScreen,
                            "View device screen in real-time", true, ""});
    mainToolWidgets.append({iDescriptorTool::QueryMobileGestalt,
                            "Query device hardware information", true, ""});
    mainToolWidgets.append({iDescriptorTool::DeveloperDiskImages,
                            "Manage developer disk images", false, ""});
    mainToolWidgets.append(
        {iDescriptorTool::WirelessGalleryImport,
         "Import photos wirelessly to your iDevice (requires Shortcuts app)",
         false, ""});
#ifndef __APPLE__
    mainToolWidgets.append({iDescriptorTool::iFuse,
                            "Mount your iPhone's filesystem on your PC", true,
                            ""});
#endif
    mainToolWidgets.append({iDescriptorTool::CableInfoWidget,
                            "View detailed cable and connection info", true,
                            ""});
    mainToolWidgets.append({iDescriptorTool::NetworkDevices,
                            "Discover and monitor devices on your network",
                            false, ""});

    for (int i = 0; i < mainToolWidgets.size(); ++i) {
        const auto &tool = mainToolWidgets[i];
        ClickableWidget *toolbox =
            createToolbox(tool.tool, tool.description, tool.requiresDevice);
        int row = i / 3;
        int col = i % 3;
        m_gridLayout->addWidget(toolbox, row, col);
    }

    contentLayout->addWidget(mainToolsWidget);

    // More Tools Section
    QLabel *moreToolsLabel = new QLabel("More Tools");
    moreToolsLabel->setStyleSheet(
        "font-weight: bold; font-size: 14px; margin-left: 10px");
    contentLayout->addWidget(moreToolsLabel);

    QWidget *moreToolsWidget = new QWidget();
    QGridLayout *moreGridLayout = new QGridLayout(moreToolsWidget);
    moreGridLayout->setSpacing(10);

    QList<iDescriptorToolWidget> moreToolWidgets;
    moreToolWidgets.append(
        {iDescriptorTool::MountDevImage,
         "Mount a compatible device image with a single click", true, ""});
    moreToolWidgets.append(
        {iDescriptorTool::Restart, "Restart device services", true, ""});
    moreToolWidgets.append(
        {iDescriptorTool::Shutdown, "Shut down the device", true, ""});
    moreToolWidgets.append({iDescriptorTool::RecoveryMode,
                            "Enter device recovery mode", true, ""});

    for (int i = 0; i < moreToolWidgets.size(); ++i) {
        const auto &tool = moreToolWidgets[i];
        ClickableWidget *toolbox =
            createToolbox(tool.tool, tool.description, tool.requiresDevice);
        int row = i / 3;
        int col = i % 3;
        moreGridLayout->addWidget(toolbox, row, col);
    }

    contentLayout->addWidget(moreToolsWidget);
    contentLayout->addStretch();

    m_scrollArea->setWidget(m_contentWidget);
    mainLayout->addWidget(m_scrollArea);

    connect(m_deviceCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &ToolboxWidget::onDeviceSelectionChanged);
}

ClickableWidget *ToolboxWidget::createToolbox(iDescriptorTool tool,
                                              const QString &description,
                                              bool requiresDevice)
{
    ClickableWidget *b = new ClickableWidget();
    b->setStyleSheet("padding: 5px; border: none; outline: none;");

    QVBoxLayout *layout = new QVBoxLayout(b);

    ZIconLabel *icon = new ZIconLabel(QIcon(), nullptr, this);
    QString title;
    switch (tool) {
    case iDescriptorTool::Airplayer:
        title = "Airplayer";
        icon->setIcon(
            QIcon(":/resources/icons/MaterialSymbolsLightAirplayOutline.png"));
        break;
    case iDescriptorTool::LiveScreen:
        title = "Live Screen";
        icon->setIcon(QIcon(":/resources/icons/PepiconsPrintCellphoneEye.png"));
        break;
    case iDescriptorTool::MountDevImage:
        title = "Mount Dev Image";
        icon->setIcon(QIcon(":/resources/icons/MdiDisk.png"));
        break;
    case iDescriptorTool::VirtualLocation:
        title = "Virtual Location";
        icon->setIcon(
            QIcon(":/resources/icons/MaterialSymbolsLocationOnOutline.png"));
        break;
    case iDescriptorTool::Restart:
        title = "Restart";
        icon->setIcon(QIcon(":/resources/icons/IcTwotoneRestartAlt.png"));
        break;
    case iDescriptorTool::Shutdown:
        title = "Shutdown";
        icon->setIcon(QIcon(":/resources/icons/IcOutlinePowerSettingsNew.png"));
        break;
    case iDescriptorTool::RecoveryMode:
        title = "Recovery Mode";
        icon->setIcon(QIcon(":/resources/icons/HugeiconsWrench01.png"));
        break;
    case iDescriptorTool::QueryMobileGestalt:
        title = "Query Mobile Gestalt";
        icon->setIcon(
            QIcon(":/resources/icons/"
                  "StreamlineProgrammingBrowserSearchSearchWindowGlassAppCod"
                  "eProgrammingQueryFindMagnifyingApps.png"));
        break;
    case iDescriptorTool::DeveloperDiskImages:
        title = "Dev Disk Images";
        icon->setIcon(QIcon(":/resources/icons/TablerDatabaseExport.png"));
        break;
    case iDescriptorTool::WirelessGalleryImport:
        title = "Wireless Gallery Import";
        icon->setIcon(
            QIcon(":/resources/icons/MaterialSymbolsAndroidWifi3BarPlus.png"));
        break;
    case iDescriptorTool::iFuse:
        title = "iFuse Mount";
        icon->setIcon(QIcon(":/resources/icons/fuse.png"));
        icon->setIconThemable(false);
        break;
    case iDescriptorTool::CableInfoWidget:
        title = "Cable Info";
        icon->setIcon(
            QIcon(":/resources/icons/MaterialSymbolsLightCableRounded.png"));
        break;
    case iDescriptorTool::NetworkDevices:
        title = "Network Devices";
        icon->setIcon(QIcon(
            ":/resources/icons/StreamlineUltimateMultipleUsersNetwork.png"));
        break;
    default:
        title = "Unknown Tool";
        break;
    }

    // Title
    QLabel *titleLabel = new QLabel(title);
    titleLabel->setAlignment(Qt::AlignCenter);

    // Description
    QLabel *descLabel = new QLabel(description);
    descLabel->setWordWrap(true);
    descLabel->setAlignment(Qt::AlignCenter);
    descLabel->setStyleSheet("color: #666; font-size: 12px;");
    icon->setFixedSize(60, 60);
    icon->setIconSize(QSize(45, 45));

    layout->addWidget(icon, 0, Qt::AlignCenter);
    layout->addWidget(titleLabel);
    layout->addWidget(descLabel);

    b->setCursor(Qt::PointingHandCursor);

    m_toolboxes.append(b);
    m_requiresDevice.append(requiresDevice);
    connect(b, &ClickableWidget::clicked,
            [this, tool]() { onToolboxClicked(tool); });
    return b;
}

void ToolboxWidget::updateDeviceList()
{
    m_deviceCombo->blockSignals(true);
    m_deviceCombo->clear();

    QList<iDescriptorDevice *> devices =
        AppContext::sharedInstance()->getAllDevices();

    if (devices.isEmpty()) {
        m_deviceCombo->addItem("No device connected");
        m_deviceCombo->setEnabled(false);
        m_uuid.clear();
    } else {
        m_deviceCombo->setEnabled(true);
        for (iDescriptorDevice *device : devices) {
            QString shortUdid =
                QString::fromStdString(device->udid).left(8) + "...";
            m_deviceCombo->addItem(
                QString::fromStdString(device->deviceInfo.productType) + " / " +
                    shortUdid +
                    (device->deviceInfo.isWireless ? " (Wi-Fi)" : ""),
                QString::fromStdString(device->udid));
        }
    }

    onCurrentDeviceChanged(
        AppContext::sharedInstance()->getCurrentDeviceSelection());

    m_deviceCombo->blockSignals(false);

    if (m_deviceCombo->count() > 0 && m_deviceCombo->currentIndex() >= 0) {
        QString currentUdid = m_deviceCombo->currentData().toString();
        if (!currentUdid.isEmpty()) {
            m_uuid = currentUdid.toStdString();
            qDebug() << "[toolboxwidget] Initialized m_uuid to:" << currentUdid;
        }
    }
}

void ToolboxWidget::updateToolboxStates()
{
    bool hasDevice = !AppContext::sharedInstance()->getAllDevices().isEmpty();

    for (int i = 0; i < m_toolboxes.size(); ++i) {
        QWidget *toolbox = m_toolboxes[i];
        bool requiresDevice = m_requiresDevice[i];

        bool enabled = !requiresDevice || hasDevice;
        toolbox->setEnabled(enabled);

        if (enabled) {
            toolbox->setStyleSheet("#toolboxFrame { "
                                   "border-radius: 5px; padding: 5px; }");
        } else {
            toolbox->setStyleSheet("#toolboxFrame { border-radius: 5px; "
                                   "padding: 5px;"
                                   "opacity: 0.45;  }");
        }
    }
}

void ToolboxWidget::updateUI()
{
    updateDeviceList();
    updateToolboxStates();
}

void ToolboxWidget::onDeviceSelectionChanged()
{
    QString selectedUdid = m_deviceCombo->currentData().toString();

    // Clear m_uuid if no valid selection
    if (selectedUdid.isEmpty()) {
        m_uuid.clear();
        return;
    }

    if (AppContext::sharedInstance()->getDevice(selectedUdid.toStdString()) ==
        nullptr) {
        QMessageBox::warning(this, "Device Not Found",
                             "The selected device is no longer connected.");
        m_uuid.clear(); // Clear stale UUID
        updateDeviceList();
        return;
    }

    m_uuid = selectedUdid.toStdString();
    qDebug() << "[toolboxwidget] Selected device UDID:" << selectedUdid;
    // Update the selected device in main menu
    AppContext::sharedInstance()->setCurrentDeviceSelection(
        DeviceSelection(selectedUdid.toStdString()));
}

void ToolboxWidget::onCurrentDeviceChanged(const DeviceSelection &selection)
{
    if (selection.valid() && selection.type == DeviceSelection::Normal) {
        int index =
            m_deviceCombo->findData(QString::fromStdString(selection.udid));
        if (index != -1) {
            // Block signals to prevent recursive calls when we update the UI
            m_deviceCombo->blockSignals(true);
            m_deviceCombo->setCurrentIndex(index);
            m_deviceCombo->blockSignals(false);

            m_uuid = selection.udid;
        }
    } else {
        // Clear m_uuid when selection is invalid
        m_uuid.clear();
    }
}

void ToolboxWidget::onToolboxClicked(iDescriptorTool tool)
{
    // final check to make sure device is connected if required
    iDescriptorDevice *device = AppContext::sharedInstance()->getDevice(m_uuid);
    if (!device && m_requiresDevice[m_toolboxes.indexOf(sender())]) {
        QMessageBox::warning(this, "Device Disconnected ?",
                             "Please select a device to use this tool.");
        return;
    }
    qDebug() << "idevice exists:" << (device != nullptr) << m_uuid.c_str();
    switch (tool) {
    case iDescriptorTool::Airplayer: {
        if (!m_airplayWindow) {
            m_airplayWindow = new AirPlayWindow();
            connect(m_airplayWindow, &QObject::destroyed, this,
                    [this]() { m_airplayWindow = nullptr; });
            m_airplayWindow->setAttribute(Qt::WA_DeleteOnClose);
            m_airplayWindow->setWindowFlag(Qt::Window);
            m_airplayWindow->resize(400, 300);
            m_airplayWindow->show();
        } else {
            m_airplayWindow->raise();
            m_airplayWindow->activateWindow();
        }
    } break;

    case iDescriptorTool::LiveScreen: {
        LiveScreenWidget *liveScreen = new LiveScreenWidget(device);
        liveScreen->setAttribute(Qt::WA_DeleteOnClose);
        liveScreen->show();
    } break;
    case iDescriptorTool::RecoveryMode: {
        // Handle entering recovery mode
        // bool success = _enterRecoveryMode(device);
        // QMessageBox msgBox;
        // msgBox.setWindowTitle("Recovery Mode");
        // if (success) {
        //     msgBox.setText("Successfully entered recovery mode.");
        // } else {
        //     msgBox.setText("Failed to enter recovery mode.");
        // }
        // msgBox.exec();
    } break;
    case iDescriptorTool::MountDevImage: {
        DevDiskImageHelper *devDiskImageHelper =
            new DevDiskImageHelper(device, this);

        connect(devDiskImageHelper, &DevDiskImageHelper::mountingCompleted,
                this, [this, devDiskImageHelper](bool success) {
                    devDiskImageHelper->deleteLater();
                    if (success) {
                        QMessageBox::information(
                            this, "Success",
                            "Developer image mounted successfully.");
                    } else {
                        QMessageBox::warning(
                            this, "Failure",
                            "Failed to mount developer image.");
                    }
                });
        devDiskImageHelper->start();
    } break;
    case iDescriptorTool::VirtualLocation: {
        // Handle virtual location functionality
        VirtualLocation *virtualLocation = new VirtualLocation(device);
        virtualLocation->setAttribute(Qt::WA_DeleteOnClose);
        virtualLocation->setWindowFlag(Qt::Window);
        virtualLocation->resize(800, 600);
        virtualLocation->show();
    } break;
    case iDescriptorTool::Restart: {
        restartDevice(device);
    } break;
    case iDescriptorTool::Shutdown: {
        shutdownDevice(device);
    } break;
    case iDescriptorTool::QueryMobileGestalt: {
        // Handle querying MobileGestalt
        QueryMobileGestaltWidget *queryMobileGestaltWidget =
            new QueryMobileGestaltWidget(device);
        queryMobileGestaltWidget->setAttribute(Qt::WA_DeleteOnClose);
        queryMobileGestaltWidget->setWindowFlag(Qt::Window);
        queryMobileGestaltWidget->resize(800, 600);
        queryMobileGestaltWidget->show();
    } break;
    case iDescriptorTool::DeveloperDiskImages: {
        if (!m_devDiskImagesWidget) {
            m_devDiskImagesWidget = new DevDiskImagesWidget(device);
            m_devDiskImagesWidget->setAttribute(Qt::WA_DeleteOnClose);
            m_devDiskImagesWidget->setWindowFlag(Qt::Window);
            m_devDiskImagesWidget->resize(800, 600);
            connect(m_devDiskImagesWidget, &QObject::destroyed, this,
                    [this]() { m_devDiskImagesWidget = nullptr; });
            m_devDiskImagesWidget->show();
        } else {
            m_devDiskImagesWidget->raise();
            m_devDiskImagesWidget->activateWindow();
        }
    } break;
    case iDescriptorTool::WirelessGalleryImport: {
        if (!m_wirelessGalleryImportWidget) {
            m_wirelessGalleryImportWidget = new WirelessGalleryImportWidget();
            connect(m_wirelessGalleryImportWidget, &QObject::destroyed, this,
                    [this]() { m_wirelessGalleryImportWidget = nullptr; });
            m_wirelessGalleryImportWidget->setAttribute(Qt::WA_DeleteOnClose);
            m_wirelessGalleryImportWidget->setWindowFlag(Qt::Window);
            // m_wirelessGalleryImportWidget->resize(800, 600);
            m_wirelessGalleryImportWidget->show();
        } else {
            m_wirelessGalleryImportWidget->raise();
            m_wirelessGalleryImportWidget->activateWindow();
        }
    } break;
#ifndef __APPLE__
    case iDescriptorTool::iFuse: {
        if (!m_ifuseWidget) {
            m_ifuseWidget = new iFuseWidget(device);
            qDebug() << "Created iFuseWidget"
                     << device->deviceInfo.productType.c_str();
            m_ifuseWidget->setAttribute(Qt::WA_DeleteOnClose);
            connect(m_ifuseWidget, &QObject::destroyed, this,
                    [this]() { m_ifuseWidget = nullptr; });
            m_ifuseWidget->setWindowFlag(Qt::Window);
            m_ifuseWidget->resize(600, 400);
            m_ifuseWidget->show();
        } else {
            m_ifuseWidget->raise();
            m_ifuseWidget->activateWindow();
        }
    } break;
#endif
    case iDescriptorTool::CableInfoWidget: {
        CableInfoWidget *cableInfoWidget = new CableInfoWidget(device);
        cableInfoWidget->setAttribute(Qt::WA_DeleteOnClose);
        cableInfoWidget->setWindowFlag(Qt::Window);
        cableInfoWidget->resize(600, 400);
        cableInfoWidget->show();
    } break;
    case iDescriptorTool::NetworkDevices: {
        if (!m_networkDevicesWidget) {
            m_networkDevicesWidget = new NetworkDevicesWidget();
            m_networkDevicesWidget->setAttribute(Qt::WA_DeleteOnClose);
            m_networkDevicesWidget->setWindowFlag(Qt::Window);
            m_networkDevicesWidget->resize(500, 600);
            connect(m_networkDevicesWidget, &QObject::destroyed, this,
                    [this]() { m_networkDevicesWidget = nullptr; });
            m_networkDevicesWidget->show();
        } else {
            m_networkDevicesWidget->raise();
            m_networkDevicesWidget->activateWindow();
        }
    } break;
    default:
        qDebug() << "Clicked on unimplemented tool";
        break;
    }
}

void ToolboxWidget::restartDevice(iDescriptorDevice *device)
{
    QMessageBox msgBox;
    msgBox.setWindowTitle("Restart Device");
    msgBox.setText("Are you sure you want to restart the device?");
    msgBox.setStandardButtons(QMessageBox::Yes | QMessageBox::No);
    msgBox.setDefaultButton(QMessageBox::No);
    int ret = msgBox.exec();
    if (ret != QMessageBox::Yes)
        return;
    if (!device || device->udid.empty()) {
        return;
    }

    // FIXME: move to servicemanager
    auto res = device->diagRelay->restart();

    if (res.is_err()) {
        QMessageBox::warning(
            nullptr, "Restart Failed",
            "Failed to restart device: " +
                QString::fromStdString(res.unwrap_err().message));
    } else {
        QMessageBox::information(nullptr, "Restart Initiated",
                                 "Device will restart once unplugged.");
        qDebug() << "Restarting device";
    }
}

void ToolboxWidget::shutdownDevice(iDescriptorDevice *device)
{

    QMessageBox msgBox;
    msgBox.setWindowTitle("Shutdown Device");
    msgBox.setText("Are you sure you want to shutdown the device?");
    msgBox.setStandardButtons(QMessageBox::Yes | QMessageBox::No);
    msgBox.setDefaultButton(QMessageBox::No);

    int ret = msgBox.exec();
    if (ret != QMessageBox::Yes)
        return;
    if (!device || device->udid.empty()) {
        return;
    }

    // FIXME: move to servicemanager
    auto res = device->diagRelay->shutdown();

    if (res.is_err()) {
        QMessageBox::warning(
            nullptr, "Shutdown Failed",
            "Failed to shutdown device: " +
                QString::fromStdString(res.unwrap_err().message));
    } else {
        QMessageBox::information(nullptr, "Shutdown Initiated",
                                 "Device will shutdown once unplugged.");
        qDebug() << "Shutting down device";
    }
}

void ToolboxWidget::_enterRecoveryMode(iDescriptorDevice *device)
{
    QMessageBox msgBox;
    msgBox.setWindowTitle("Enter Recovery Mode");
    msgBox.setText("Are you sure you want to enter recovery mode?");
    msgBox.setStandardButtons(QMessageBox::Yes | QMessageBox::No);
    msgBox.setDefaultButton(QMessageBox::No);

    int ret = msgBox.exec();
    if (ret != QMessageBox::Yes)
        return;

    if (!device || device->udid.empty()) {
        return;
    }

    // auto res = device->diagRelay->enterRecovery();
    // if (res.is_err()) {
    //     QMessageBox::warning(
    //         nullptr, "Enter Recovery Mode Failed",
    //         "Failed to enter recovery mode: " +
    //             QString::fromStdString(res.unwrap_err().message));
    // } else {
    //     QMessageBox::information(nullptr, "Enter Recovery Mode Initiated",
    //                              "Device will enter recovery mode once
    //                              unplugged.");
    //     qDebug() << "Entering recovery mode";
    // }
}