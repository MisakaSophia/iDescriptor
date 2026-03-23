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

#include "sshterminaltool.h"
#include "appcontext.h"
#include "responsiveqlabel.h"
#include "sshterminalwidget.h"

SSHTerminalTool::SSHTerminalTool(QWidget *parent)
    : Tool{parent}, m_selectedUniq(QString(""), false)
{
    setWindowTitle("SSH Terminal - iDescriptor");
    QHBoxLayout *mainLayout = new QHBoxLayout(this);
    mainLayout->setContentsMargins(2, 2, 2, 2);
    mainLayout->setSpacing(2);

    // Create responsive image label
    ResponsiveQLabel *deviceImageLabel = new ResponsiveQLabel(this);
    deviceImageLabel->setPixmap(QPixmap(":/resources/iphone.png"));
    deviceImageLabel->setMinimumWidth(200);
    deviceImageLabel->setSizePolicy(QSizePolicy::Ignored,
                                    QSizePolicy::Expanding);
    deviceImageLabel->setStyleSheet("background: transparent; border: none;");

    mainLayout->addWidget(deviceImageLabel, 1);

    connect(AppContext::sharedInstance(), &AppContext::deviceAdded, this,
            &SSHTerminalTool::On_iDeviceAdded);
    connect(AppContext::sharedInstance(), &AppContext::deviceRemoved, this,
            &SSHTerminalTool::On_iDeviceRemoved);

    connect(NetworkDeviceProvider::sharedInstance(),
            &NetworkDeviceProvider::deviceAdded, this,
            &SSHTerminalTool::onWirelessDeviceAdded);
    connect(NetworkDeviceProvider::sharedInstance(),
            &NetworkDeviceProvider::deviceRemoved, this,
            &SSHTerminalTool::onWirelessDeviceRemoved);

    QWidget *rightContainer = new QWidget();
    rightContainer->setSizePolicy(QSizePolicy::Expanding,
                                  QSizePolicy::Expanding);
    rightContainer->setMinimumWidth(400);
    QVBoxLayout *rightLayout = new QVBoxLayout(rightContainer);
    rightLayout->setContentsMargins(15, 15, 15, 15);
    rightLayout->setSpacing(10);

    setupDeviceSelectionUI(rightLayout);

    mainLayout->addWidget(rightContainer, 3);

    updateDeviceList();
}

void SSHTerminalTool::setupDeviceSelectionUI(QVBoxLayout *layout)
{
    // Create scroll area for device selection
    QScrollArea *scrollArea = new QScrollArea();
    scrollArea->setWidgetResizable(true);
    scrollArea->setMinimumHeight(200);
    scrollArea->setMaximumHeight(300);
    scrollArea->setObjectName("devicescrollArea");

    // scrollArea->setStyleSheet("QWidget#devicescrollArea {border: none;}");
    QWidget *scrollContent = new QWidget();
    m_deviceLayout = new QVBoxLayout(scrollContent);
    m_deviceLayout->setContentsMargins(5, 5, 5, 5);
    m_deviceLayout->setSpacing(10);

    // Button group for device selection
    m_deviceButtonGroup = new QButtonGroup(this);
    connect(m_deviceButtonGroup,
            QOverload<QAbstractButton *>::of(&QButtonGroup::buttonClicked),
            this, &SSHTerminalTool::onDeviceSelected);

    // Wired devices group
    m_connectedDevicesGroup = new QGroupBox("Connected Devices");
    m_connectedDevicesLayout = new QVBoxLayout(m_connectedDevicesGroup);
    m_deviceLayout->addWidget(m_connectedDevicesGroup);

    // Wireless devices group
    m_networkDevicesGroup = new QGroupBox("Network (All) Devices");
    m_networkDevicesLayout = new QVBoxLayout(m_networkDevicesGroup);
    m_deviceLayout->addWidget(m_networkDevicesGroup);

    scrollArea->setWidget(scrollContent);
    layout->addWidget(scrollArea);

    // Info and connect button
    m_infoLabel = new QLabel("Select a device to connect");
    layout->addWidget(m_infoLabel);

    // support
    m_connectButton = new QPushButton("Choose a device");
    m_connectButton->setMaximumWidth(m_connectButton->sizeHint().width());
    m_connectButton->setEnabled(false);
    connect(m_connectButton, &QPushButton::clicked, this,
            &SSHTerminalTool::onOpenSSHTerminal);
    layout->addWidget(m_connectButton, 0, Qt::AlignCenter);

    // Manual IP connection group
    m_manualConnectionGroup = new QGroupBox("Manual IP connection");
    QVBoxLayout *manualLayout = new QVBoxLayout(m_manualConnectionGroup);

    QHBoxLayout *ipLayout = new QHBoxLayout();
    QLabel *ipLabel = new QLabel("Device IP:");
    m_manualIpEdit = new QLineEdit();
    m_manualIpEdit->setPlaceholderText("e.g. 192.168.1.10");

    ipLayout->addWidget(ipLabel);
    ipLayout->addWidget(m_manualIpEdit);

    m_manualConnectButton = new QPushButton("Connect via IP");
    connect(m_manualConnectButton, &QPushButton::clicked, this,
            &SSHTerminalTool::onManualConnectClicked);

    manualLayout->addLayout(ipLayout);
    manualLayout->addWidget(m_manualConnectButton, 0, Qt::AlignLeft);

    layout->addWidget(m_manualConnectionGroup);
}

void SSHTerminalTool::updateDeviceList()
{
    // Clear existing devices
    clearDeviceButtons();

    // Add wired devices
    QList<iDescriptorDevice *> wiredDevices =
        AppContext::sharedInstance()->getAllDevices();
    for (iDescriptorDevice *device : wiredDevices) {
        addDevice(device);
    }

    // Add wireless devices
    QList<NetworkDevice> wirelessDevices =
        NetworkDeviceProvider::sharedInstance()->getNetworkDevices();
    for (const NetworkDevice &device : wirelessDevices) {
        addWirelessDevice(device);
    }
}

void SSHTerminalTool::clearDeviceButtons()
{
    // Remove all buttons from button group and layouts
    for (QAbstractButton *button : m_deviceButtonGroup->buttons()) {
        m_deviceButtonGroup->removeButton(button);
        // button->deleteLater();
    }

    // // Clear layouts
    // QLayoutItem *item;
    // while ((item = m_deviceLayout->takeAt(0)) != nullptr) {
    //     delete item->widget();
    //     delete item;
    // }
    // while ((item = m_networkDevicesLayout->takeAt(0)) != nullptr) {
    //     delete item->widget();
    //     delete item;
    // }

    auto clearLayout = [](QLayout *layout) {
        QLayoutItem *item;
        while ((item = layout->takeAt(0)) != nullptr) {
            if (QWidget *w = item->widget()) {
                w->deleteLater();
            }
            delete item;
        }
    };

    // Only clear the inner layouts, keep the group boxes and m_deviceLayout
    clearLayout(m_connectedDevicesLayout);
    clearLayout(m_networkDevicesLayout);
}

// FIXME: should not add already exiting devices
void SSHTerminalTool::addDevice(const iDescriptorDevice *device)
{
    QString deviceName = QString::fromStdString(device->deviceInfo.deviceName);
    QString udid = QString::fromStdString(device->udid);
    QString displayText = QString("%1\n%2").arg(deviceName, udid);

    QRadioButton *radioButton = new QRadioButton(displayText);
    if (device->deviceInfo.isWireless) {
        radioButton->setProperty(
            "deviceName",
            QString::fromStdString(device->deviceInfo.deviceName));
        radioButton->setProperty(
            "uniq", QString::fromStdString(device->deviceInfo.wifiMacAddress));
        radioButton->setProperty(
            "ip", QString::fromStdString(device->deviceInfo.ipAddress));
        // workaround
        radioButton->setProperty("udid", QString::fromStdString(device->udid));

        radioButton->setProperty("deviceType", "wireless");
    } else {
        radioButton->setProperty("uniq", udid);
        radioButton->setProperty("deviceType", "wired");
    }

    m_deviceButtonGroup->addButton(radioButton);
    m_connectedDevicesLayout->addWidget(radioButton);
}

void SSHTerminalTool::addWirelessDevice(const NetworkDevice &device)
{
    QString displayText = QString("%1\n%2").arg(device.name, device.address);

    QRadioButton *radioButton = new QRadioButton(displayText);
    radioButton->setProperty("deviceName", device.name);
    radioButton->setProperty("uniq", device.macAddress);
    radioButton->setProperty("ip", device.address);
    radioButton->setProperty("deviceType", "wireless");

    m_deviceButtonGroup->addButton(radioButton);
    m_networkDevicesLayout->addWidget(radioButton);
}
// m_selectedUniq
void SSHTerminalTool::On_iDeviceAdded(const iDescriptorDevice *device)
{
    addDevice(device);
}

void SSHTerminalTool::On_iDeviceRemoved(const std::string &udid)
{
    QString qudid = QString::fromStdString(udid);

    // Find and remove the corresponding radio button
    for (QAbstractButton *button : m_deviceButtonGroup->buttons()) {
        if (button->property("udid").toString() == qudid) {
            m_deviceButtonGroup->removeButton(button);
            button->deleteLater();
            break;
        }
    }

    // Reset selection if this device was selected
    // if (m_selectedUniq == qudid) {
    //     resetSelection();
    // }
}

void SSHTerminalTool::onWirelessDeviceAdded(const NetworkDevice &device)
{
    addWirelessDevice(device);
}

void SSHTerminalTool::onWirelessDeviceRemoved(const QString &deviceName)
{
    // Find and remove the corresponding radio button
    for (QAbstractButton *button : m_deviceButtonGroup->buttons()) {
        if (button->property("deviceType").toString() == "wireless" &&
            button->property("deviceName").toString() == deviceName) {
            m_deviceButtonGroup->removeButton(button);
            button->deleteLater();
            break;
        }
    }

    // // Reset selection if this device was selected
    // if (m_selectedDeviceType == DeviceType::Wireless &&
    //     m_selectedNetworkDevice.name == deviceName) {
    //     resetSelection();
    // }
}

void SSHTerminalTool::onDeviceSelected(QAbstractButton *button)
{
    QString deviceType = button->property("deviceType").toString();
    QString ip = button->property("ip").toString();
    bool isWireless = (deviceType == "wireless");

    m_selectedUniq =
        iDescriptor::Uniq(button->property("uniq").toString(), isWireless);

    // required for wireless devices
    if (!ip.isEmpty()) {
        m_selectedUniq.setIP(ip);
    }

    iDescriptorDevice *selectedDevice =
        AppContext::sharedInstance()->getDevice(m_selectedUniq);

    if (!selectedDevice) {
        QString udid = button->property("udid").toString();
        if (!udid.isEmpty()) {
            iDescriptorDevice *selectedDevice =
                AppContext::sharedInstance()->getDevice(udid.toStdString());
            if (selectedDevice) {
                if (selectedDevice->deviceInfo.jailbroken) {
                    m_infoLabel->setText(
                        "Device selected (detected as jailbroken)");
                } else {
                    m_infoLabel->setText(
                        "Device selected (detected as non-jailbroken)");
                }
            }
        } else {
            // device may get removed just as user clicks,
            // so we'll treat it as unknown
            m_infoLabel->setText(
                "Network device selected (jailbreak status unknown)");
        }
    } else {
        if (selectedDevice->deviceInfo.jailbroken) {
            m_infoLabel->setText("Device selected (detected as jailbroken)");
        } else {
            m_infoLabel->setText(
                "Device selected (detected as non-jailbroken)");
        }
    }

    m_connectButton->setEnabled(true);
    m_connectButton->setText("Connect");
}

void SSHTerminalTool::onManualConnectClicked()
{
    QString ip = m_manualIpEdit->text().trimmed();
    if (ip.isEmpty()) {
        m_infoLabel->setText("Please enter an IP address for manual connect");
        QMessageBox::warning(this, "Missing IP Address",
                             "Please enter a device IP address.");
        return;
    }

    // Treat manual connection as wireless with unknown jailbreak status
    m_selectedUniq.set(ip, true); // mark as "wireless"-style
    m_selectedUniq.setIP(ip);

    m_infoLabel->setText(
        QString("Manual IP %1 selected (jailbreak status unknown)").arg(ip));

    // Reuse normal connect logic
    onOpenSSHTerminal();
}

void SSHTerminalTool::resetSelection()
{
    // m_selectedDeviceType = DeviceType::None;
    // m_selectedWiredDevice = nullptr;
    // m_selectedNetworkDevice = NetworkDevice{};
    m_connectButton->setEnabled(false);
    m_infoLabel->setText("Select a device to connect");

    // Uncheck all radio buttons
    if (m_deviceButtonGroup->checkedButton()) {
        m_deviceButtonGroup->setExclusive(false);
        m_deviceButtonGroup->checkedButton()->setChecked(false);
        m_deviceButtonGroup->setExclusive(true);
    }
}

void SSHTerminalTool::onOpenSSHTerminal()
{
    if (m_selectedUniq.get().isEmpty()) {
        m_infoLabel->setText("Please select a device first");
        QMessageBox::warning(
            this, "No Device Selected",
            "Please select a device before trying to connect.");
        return;
    }

    // Warn if known device is not jailbroken
    iDescriptorDevice *selectedDevice =
        AppContext::sharedInstance()->getDevice(m_selectedUniq);
    if (selectedDevice && !selectedDevice->deviceInfo.jailbroken) {
        auto reply = QMessageBox::question(
            this, "Device Not Jailbroken",
            "The selected device is not detected as jailbroken.\n"
            "SSH access may not be available.\n\n"
            "Do you want to continue anyway?",
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
        if (reply == QMessageBox::No) {
            m_infoLabel->setText(
                "Connection cancelled (device not jailbroken)");
            return;
        }
    }

    bool isWireless = m_selectedUniq.isMac();

    // there must be an IP for wireless devices, check just in case
    if (isWireless && m_selectedUniq.getIP().isEmpty()) {
        m_infoLabel->setText(
            "Selected network device is missing IP address. Please try again.");
        QMessageBox::warning(this, "Missing IP Address",
                             "The selected network device is missing an IP "
                             "address. Please try again.");
        return;
    }

    // Prepare connection info
    ConnectionInfo connectionInfo;

    if (!isWireless) {
        connectionInfo.type = ConnectionType::Wired;
        connectionInfo.deviceName = "FIXME";
        connectionInfo.deviceUdid = m_selectedUniq.get();
        connectionInfo.hostAddress = "127.0.0.1";
        connectionInfo.port = 22;
    } else {
        connectionInfo.type = ConnectionType::Wireless;
        connectionInfo.deviceName = m_selectedUniq.get();
        connectionInfo.deviceUdid = "";
        connectionInfo.hostAddress = m_selectedUniq.getIP();
        connectionInfo.port = 22;
    }

    // Create and show SSH terminal widget in a new window
    SSHTerminalWidget *sshTerminal = new SSHTerminalWidget(connectionInfo);
    sshTerminal->setAttribute(Qt::WA_DeleteOnClose);
    sshTerminal->show();
    sshTerminal->raise();
    sshTerminal->activateWindow();
}

SSHTerminalTool::~SSHTerminalTool() {}
