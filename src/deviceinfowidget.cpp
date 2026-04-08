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

#include "deviceinfowidget.h"
#include "batterywidget.h"
#include "diskusagewidget.h"
#include "iDescriptor-ui.h"
#include "iDescriptor.h"
#include "infolabel.h"
#include "privateinfolabel.h"
#include "toolboxwidget.h"

DeviceInfoWidget::DeviceInfoWidget(
    const std::shared_ptr<iDescriptorDevice> device, QWidget *parent)
    : QWidget(parent), m_device(device)
{
    // Main layout with horizontal orientation
    QHBoxLayout *mainLayout = new QHBoxLayout(this);
    mainLayout->setContentsMargins(0, 0, 10, 0);
    mainLayout->setSpacing(1);

    // Left side container for image and actions
    QWidget *leftContainer = new QWidget();
    // leftContainer->setStyleSheet("margin-left: 100px");
    QVBoxLayout *leftLayout = new QVBoxLayout(leftContainer);
    leftLayout->setContentsMargins(0, 0, 0, 0);
    leftLayout->setSpacing(1);

    // Create responsive device image widget
    m_deviceImageWidget = new DeviceImageWidget(device, this);

    // Actions group box
    QWidget *actionsWidget = new QWidget();
    actionsWidget->setObjectName("actionsWidget");
    actionsWidget->setFixedHeight(40);
    actionsWidget->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    actionsWidget->setStyleSheet(
        "QWidget#actionsWidget { background: transparent; border: none; }");
    QHBoxLayout *actionsLayout = new QHBoxLayout(actionsWidget);
    actionsLayout->setContentsMargins(1, 1, 1, 1);
    actionsLayout->setSpacing(10);

    ZIconWidget *shutdownBtn = new ZIconWidget(
        QIcon(":/resources/icons/IcOutlinePowerSettingsNew.png"), "Shutdown",
        1.0, this);
    shutdownBtn->setIconSize(QSize(20, 20));
    connect(shutdownBtn, &ZIconWidget::clicked, this,
            [device]() { ToolboxWidget::shutdownDevice(device); });

    ZIconWidget *restartBtn =
        new ZIconWidget(QIcon(":/resources/icons/IcTwotoneRestartAlt.png"),
                        "Restart", 1.0, this);
    restartBtn->setIconSize(QSize(20, 20));
    connect(restartBtn, &ZIconWidget::clicked, this,
            [device]() { ToolboxWidget::restartDevice(device); });

    ZIconWidget *recoveryBtn =
        new ZIconWidget(QIcon(":/resources/icons/HugeiconsWrench01.png"),
                        "Recovery", 1.0, this);
    recoveryBtn->setIconSize(QSize(20, 20));
    connect(recoveryBtn, &ZIconWidget::clicked, this,
            [device]() { ToolboxWidget::enterRecoveryMode(device); });

    actionsLayout->addWidget(shutdownBtn);
    actionsLayout->addWidget(restartBtn);
    actionsLayout->addWidget(recoveryBtn);

    leftLayout->addStretch();
    leftLayout->addWidget(m_deviceImageWidget);
    leftLayout->addWidget(actionsWidget, 0, Qt::AlignCenter);
    leftLayout->addStretch();

    // Add stretches around leftContainer to center it horizontally
    mainLayout->addStretch();
    mainLayout->addWidget(leftContainer);
    mainLayout->addStretch();

    // Right side: Info Table
    QWidget *infoContainer = new QWidget();
    infoContainer->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Maximum);

    QVBoxLayout *infoLayout = new QVBoxLayout(infoContainer);

    // Header
    QGroupBox *headerWidget = new QGroupBox();
    QHBoxLayout *headerLayout = new QHBoxLayout(headerWidget);
    headerLayout->setContentsMargins(5, 5, 5, 5);
    headerLayout->setSpacing(15);

    QLabel *devProductType =
        new QLabel(QString::fromStdString(device->deviceInfo.productType));
    devProductType->setToolTip(
        QString::fromStdString(device->deviceInfo.marketingName));
#ifndef WIN32
    devProductType->setStyleSheet("font-size: 1rem; font-weight: bold;");
#else
    devProductType->setStyleSheet(
        mergeStyles(devProductType, "font-size: 18px; font-weight: 500;"));
#endif

    QLabel *diskCapacityLabel = new QLabel(
        QString::number(device->deviceInfo.diskInfo.totalDiskCapacity /
                        (1000 * 1000 * 1000)) +
        " GB");

    diskCapacityLabel->setSizePolicy(QSizePolicy::Maximum,
                                     QSizePolicy::Preferred);
    diskCapacityLabel->setAttribute(Qt::WA_StyledBackground, true);
    diskCapacityLabel->setStyleSheet(QString("background-color: %1;"
                                             "padding: 2px 4px;"
                                             "color : white;"
                                             "border-radius: 13px;")
                                         .arg(COLOR_ACCENT_BLUE.name()));

    m_chargingStatusLabel =
        new QLabel(device->deviceInfo.batteryInfo.isCharging ? "Charging"
                                                             : "Not Charging");
    // Create the layout without a parent widget
    QHBoxLayout *chargingLayout = new QHBoxLayout();
    chargingLayout->setContentsMargins(0, 0, 0, 0);
    chargingLayout->setSpacing(5);

    // Create icon label
    m_lightningIconLabel =
        new ZIconLabel(QIcon(":/resources/icons/MdiLightningBolt.png"),
                       " Charging", 1.0, this);
    updateChargingStatusIcon();

    m_batteryWidget = new BatteryWidget(
        qBound<int>(1, device->deviceInfo.batteryInfo.currentBatteryLevel, 100),
        device->deviceInfo.batteryInfo.isCharging, this);

    // Add the widgets to the new layout
    chargingLayout->addWidget(m_chargingStatusLabel);
    chargingLayout->addWidget(m_lightningIconLabel);
    chargingLayout->addWidget(m_batteryWidget);

    m_chargingWattsWithCableTypeLabel = new QLabel(
        QString::number(device->deviceInfo.batteryInfo.watts) + "W" + "/" +
        (device->deviceInfo.batteryInfo.usbConnectionType ==
                 BatteryInfo::ConnectionType::USB
             ? "USB"
             : "USB-C"));

    headerLayout->addWidget(devProductType);
    headerLayout->addWidget(diskCapacityLabel);
    headerLayout->addStretch(); // Push items to the left
    headerLayout->addLayout(chargingLayout);
    headerLayout->addWidget(m_chargingWattsWithCableTypeLabel);

    infoLayout->addWidget(headerWidget);
    // add spacer
    infoLayout->addSpacerItem(
        new QSpacerItem(20, 40, QSizePolicy::Minimum, QSizePolicy::Expanding));
    // Add maximum stretch between header and grid
    infoLayout->addStretch();

    QGroupBox *gridContainer = new QGroupBox("Device Information");
    gridContainer->setSizePolicy(QSizePolicy::Expanding,
                                 QSizePolicy::Preferred);
    QGridLayout *gridLayout = new QGridLayout(); // Set layout on gridWidget
    gridLayout->setSpacing(8);
    gridLayout->setColumnStretch(1, 1); // Allow value column to stretch
    gridLayout->setColumnStretch(
        3, 1); // Allow value column for right side to stretch
    gridLayout->setContentsMargins(17, 17, 17, 17);
    gridContainer->setLayout(gridLayout);
    QList<QPair<QString, QWidget *>> infoItems;

    auto createValueLabel = [](const QString &text) {
        return new InfoLabel(text);
    };

    infoItems.append({"iOS Version:", createValueLabel(QString::fromStdString(
                                          device->deviceInfo.productVersion))});
    infoItems.append({"Device Name:", createValueLabel(QString::fromStdString(
                                          device->deviceInfo.deviceName))});

    // Activation state label with color and tooltip
    QLabel *activationLabel = new QLabel;
    QString stateText;
    QString tooltipText;
    QColor color;

    switch (device->deviceInfo.activationState) {
    case DeviceInfo::ActivationState::Activated:
        stateText = "Activated";
        color = COLOR_GREEN;
        tooltipText = "Device is activated and ready for use.";
        break;
    case DeviceInfo::ActivationState::FactoryActivated:
        stateText = "Factory Activated";
        color = COLOR_ORANGE;
        tooltipText = "Activation is most likely bypassed.";
        break;
    default:
        stateText = "Unactivated";
        color = COLOR_RED;
        tooltipText = "Device is not activated and requires setup.";
        break;
    }

    activationLabel->setText(stateText);
    activationLabel->setStyleSheet("color: " + color.name() + ";");
    activationLabel->setToolTip(tooltipText);
    infoItems.append({"Activation State:", activationLabel});

    infoItems.append({"Device Class:", createValueLabel(QString::fromStdString(
                                           device->deviceInfo.deviceClass))});
    infoItems.append(
        {"Jailbroken:", createValueLabel(QString::fromStdString(
                            device->deviceInfo.jailbroken ? "Yes" : "No"))});
    infoItems.append({"Model Number:", createValueLabel(QString::fromStdString(
                                           device->deviceInfo.modelNumber))});
    infoItems.append(
        {"CPU Architecture:", createValueLabel(QString::fromStdString(
                                  device->deviceInfo.cpuArchitecture))});
    infoItems.append({"Build Version:", createValueLabel(QString::fromStdString(
                                            device->deviceInfo.buildVersion))});
    infoItems.append(
        {"Hardware Model:", createValueLabel(QString::fromStdString(
                                device->deviceInfo.hardwareModel))});
    infoItems.append({"Region:", createValueLabel(QString::fromStdString(
                                     device->deviceInfo.region))});
    infoItems.append(
        {"Hardware Platform:", createValueLabel(QString::fromStdString(
                                   device->deviceInfo.hardwarePlatform))});
    // infoItems.append(
    //     {"Battery Cycle:", createValueLabel(QString::number(
    //                            m_device->deviceInfo.batteryInfo.cycleCount))});
    infoItems.append(
        {"Firmware Version:", createValueLabel(QString::fromStdString(
                                  device->deviceInfo.firmwareVersion))});

    // FIXME: Battery Info
    QWidget *batteryWidget = new QWidget();
    QHBoxLayout *batteryLayout = new QHBoxLayout(batteryWidget);
    batteryLayout->setContentsMargins(0, 0, 0, 0);
    batteryLayout->setSpacing(5);
    batteryLayout->addWidget(new QLabel(device->deviceInfo.batteryInfo.health));
    QPushButton *moreButton = new QPushButton("More");
    moreButton->setStyleSheet(mergeStyles(
        moreButton,
        "QPushButton { height : 20px; min-height: 20px; padding: 2px "
        "8px; font-size: 10px; }"));
    connect(moreButton, &QPushButton::clicked, this,
            &DeviceInfoWidget::onBatteryMoreClicked);
    batteryLayout->addWidget(moreButton);
    batteryLayout->addStretch();
    infoItems.append({"Battery Health:", batteryWidget});

    infoItems.append(
        {"Production Device:",
         createValueLabel(QString::fromStdString(
             device->deviceInfo.productionDevice ? "Yes" : "No"))});

    // Serial Number with privacy
    if (!device->deviceInfo.serialNumber.empty()) {
        infoItems.append(
            {"Serial Number:",
             new PrivateInfoLabel(
                 QString::fromStdString(device->deviceInfo.serialNumber),
                 this)});
    }

    // IMEI with privacy (Mobile Equipment Identifier)
    if (!device->deviceInfo.mobileEquipmentIdentifier.empty()) {
        infoItems.append(
            {"IMEI:", new PrivateInfoLabel(
                          QString::fromStdString(
                              device->deviceInfo.mobileEquipmentIdentifier),
                          this)});
    }

    // Distribute items into the grid
    int numRows = (infoItems.size() + 1) / 2;
    for (int i = 0; i < numRows; ++i) {
        // Left column item
        QLabel *keyLabelLeft = new QLabel(infoItems[i].first);
#ifndef WIN32
        keyLabelLeft->setStyleSheet("font-weight: bold;");
#else
        keyLabelLeft->setStyleSheet("font-size: 15px; font-weight: 500;");
#endif
        gridLayout->addWidget(keyLabelLeft, i, 0);
        gridLayout->addWidget(infoItems[i].second, i, 1);

        // Right column item
        int rightIndex = i + numRows;
        if (rightIndex < infoItems.size()) {
            QLabel *keyLabelRight = new QLabel(infoItems[rightIndex].first);
#ifndef WIN32
            keyLabelRight->setStyleSheet("font-weight: bold;");
#else
            keyLabelRight->setStyleSheet("font-size: 15px; font-weight: 500;");
#endif
            gridLayout->addWidget(keyLabelRight, i, 2);
            gridLayout->addWidget(infoItems[rightIndex].second, i, 3);
        }
    }

    infoLayout->addWidget(gridContainer);
    infoLayout->addStretch(); // Pushes footer to the bottom

    // Footer
    QLabel *footerLabel = new QLabel("UDID: " + device->udid);
    footerLabel->setToolTip("Unique Device Identifier");
    footerLabel->setStyleSheet(
        "font-size: 10px; color: #666; margin-top: 5px; ");
    footerLabel->setWordWrap(true);
    infoLayout->addWidget(footerLabel);

    QVBoxLayout *rightSideLayout = new QVBoxLayout();
    rightSideLayout->setSpacing(10);
    rightSideLayout->addStretch();

    rightSideLayout->addWidget(infoContainer);
    rightSideLayout->addWidget(new DiskUsageWidget(device, this));

    rightSideLayout->addStretch();

    mainLayout->addLayout(rightSideLayout);
    mainLayout->addStretch();

    connect(m_device->service_manager,
            &CXX::ServiceManager::battery_info_updated, this,
            &DeviceInfoWidget::updateBatteryInfo);
}

DeviceInfoWidget::~DeviceInfoWidget() {}

void DeviceInfoWidget::onBatteryMoreClicked()
{
    QMessageBox msgBox;
    msgBox.setWindowTitle("Battery Details");
    QString details =
        "Battery Cycle Count: " +
        QString::number(m_device->deviceInfo.batteryInfo.cycleCount) + "\n" +
        "Battery Serial Number: " +
        QString::fromStdString(m_device->deviceInfo.batteryInfo.serialNumber);
    msgBox.setText(details);
    msgBox.exec();
}

void DeviceInfoWidget::updateBatteryInfo(const QString &diagnostics)
{
    /*DATA*/
    DeviceInfo &d = const_cast<DeviceInfo &>(m_device->deviceInfo);
    pugi::xml_document doc;
    auto res = doc.load_string(diagnostics.toUtf8().constData());
    if (!res) {
        qDebug() << "Failed to parse battery info XML:";
        return;
    }
    auto xml_doc = XmlPlistDict(doc.child("plist").child("dict"));
    if (d.oldDevice)
        parseOldDeviceBattery(xml_doc, d);
    else
        parseDeviceBattery(xml_doc, d);
    /*UI*/
    updateChargingStatusIcon();
    m_chargingWattsWithCableTypeLabel->setText(
        QString::number(d.batteryInfo.watts) + "W" + "/" +
        (d.batteryInfo.usbConnectionType == BatteryInfo::ConnectionType::USB
             ? "USB"
             : "USB-C"));

    m_batteryWidget->updateContext(
        d.batteryInfo.isCharging,
        qBound<int>(1, d.batteryInfo.currentBatteryLevel, 100));
}

void DeviceInfoWidget::updateChargingStatusIcon()
{
    if (m_device->deviceInfo.batteryInfo.isCharging) {
        m_chargingStatusLabel->setText("Charging");
        m_chargingStatusLabel->setStyleSheet(
            QString("color: %1;").arg(COLOR_GREEN.name()));
        m_lightningIconLabel->show();

    } else {
        m_chargingStatusLabel->setText("Not Charging");
        m_chargingStatusLabel->setStyleSheet("");
        m_lightningIconLabel->hide();
    }
}
