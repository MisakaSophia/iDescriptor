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

#include "networkdevicestoconnectwidget.h"

#ifdef __linux__
#include "core/services/avahi/avahi_service.h"
#else
#include "core/services/dnssd/dnssd_service.h"
#endif

#include "appcontext.h"
#include <QApplication>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QPalette>
#include <QPushButton>

NetworkDevicesToConnectWidget::NetworkDevicesToConnectWidget(QWidget *parent)
    : QWidget(parent)
{
    setupUI();

#ifdef __linux__
    m_networkProvider = new AvahiService(this);
    connect(m_networkProvider, &AvahiService::deviceAdded, this,
            &NetworkDevicesToConnectWidget::onWirelessDeviceAdded);
    connect(m_networkProvider, &AvahiService::deviceRemoved, this,
            &NetworkDevicesToConnectWidget::onWirelessDeviceRemoved);
#else
    m_networkProvider = new DnssdService(this);
    connect(m_networkProvider, &DnssdService::deviceAdded, this,
            &NetworkDevicesToConnectWidget::onWirelessDeviceAdded);
    connect(m_networkProvider, &DnssdService::deviceRemoved, this,
            &NetworkDevicesToConnectWidget::onWirelessDeviceRemoved);
#endif

    // Start scanning for network devices
    m_networkProvider->startBrowsing();

    // Initial device list update
    updateDeviceList();
}

NetworkDevicesToConnectWidget::~NetworkDevicesToConnectWidget()
{
    if (m_networkProvider) {
        m_networkProvider->stopBrowsing();
    }
}

void NetworkDevicesToConnectWidget::setupUI()
{
    QVBoxLayout *mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(10, 10, 10, 10);
    mainLayout->setSpacing(10);

    // Status label
    m_statusLabel = new QLabel("Scanning for network devices...");
    QFont statusFont = m_statusLabel->font();
    statusFont.setPointSize(12);
    statusFont.setWeight(QFont::Medium);
    m_statusLabel->setFont(statusFont);
    m_statusLabel->setAlignment(Qt::AlignCenter);
    mainLayout->addWidget(m_statusLabel);

    // Device group
    m_deviceGroup = new QGroupBox("Network Devices");
    QFont groupFont = m_deviceGroup->font();
    groupFont.setPointSize(14);
    groupFont.setWeight(QFont::Bold);
    m_deviceGroup->setFont(groupFont);

    QVBoxLayout *groupLayout = new QVBoxLayout(m_deviceGroup);
    groupLayout->setContentsMargins(5, 15, 5, 5);
    groupLayout->setSpacing(0);

    // Scroll area
    m_scrollArea = new QScrollArea();
    m_scrollArea->setWidgetResizable(true);
    m_scrollArea->setMinimumHeight(200);
    m_scrollArea->setMaximumHeight(400);
    m_scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_scrollArea->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    m_scrollArea->setStyleSheet(
        "QScrollArea { background: transparent; border: none; }");
    /* FIXME: We need a better approach to theme awareness   */
    connect(qApp, &QApplication::paletteChanged, this, [this]() {
        m_scrollArea->setStyleSheet(
            "QScrollArea { background: transparent; border: none; }");
    });

    // Scroll content
    m_scrollContent = new QWidget();
    m_scrollContent->setContentsMargins(0, 0, 0, 0);
    m_deviceLayout = new QVBoxLayout(m_scrollContent);
    m_deviceLayout->setContentsMargins(5, 5, 5, 5);
    m_deviceLayout->setSpacing(8);
    m_deviceLayout->addStretch();

    m_scrollArea->setWidget(m_scrollContent);
    groupLayout->addWidget(m_scrollArea);

    mainLayout->addWidget(m_deviceGroup);
    mainLayout->addStretch();
}

void NetworkDevicesToConnectWidget::createDeviceCard(
    const NetworkDevice &device)
{
    // Main card frame
    QWidget *card = new QWidget();

    QVBoxLayout *cardLayout = new QVBoxLayout(card);
    cardLayout->setContentsMargins(12, 10, 12, 10);
    cardLayout->setSpacing(4);

    // Device name (primary)
    QLabel *nameLabel = new QLabel(device.name);
    nameLabel->setWordWrap(true);
    QFont nameFont = nameLabel->font();
    nameFont.setPointSize(13);
    nameFont.setWeight(QFont::Medium);
    nameLabel->setFont(nameFont);
    QPalette namePalette = nameLabel->palette();
    namePalette.setColor(QPalette::WindowText,
                         palette().color(QPalette::WindowText));
    nameLabel->setPalette(namePalette);

    // Device info container
    QWidget *infoContainer = new QWidget();
    QHBoxLayout *infoLayout = new QHBoxLayout(infoContainer);
    infoLayout->setContentsMargins(0, 0, 0, 0);
    infoLayout->setSpacing(12);

    // Address info
    QLabel *addressLabel = new QLabel(QString("IP: %1").arg(device.address));
    QFont addressFont = addressLabel->font();
    addressFont.setPointSize(11);
    addressLabel->setFont(addressFont);
    QPalette addressPalette = addressLabel->palette();
    QColor secondaryColor = palette().color(QPalette::WindowText);
    secondaryColor.setAlpha(180);
    addressPalette.setColor(QPalette::WindowText, secondaryColor);
    addressLabel->setPalette(addressPalette);

    // Port info
    QLabel *portLabel = new QLabel(QString("Port: %1").arg(device.port));
    portLabel->setFont(addressFont);
    portLabel->setPalette(addressPalette);

    infoLayout->addWidget(addressLabel);
    infoLayout->addWidget(portLabel);
    infoLayout->addStretch();

    QPushButton *connectButton = new QPushButton("Connect");
    connectButton->setDefault(true);
    connect(connectButton, &QPushButton::clicked, this,
            [this, device, connectButton]() {
                connectButton->setText("Connecting...");
                connectButton->setEnabled(false);
                AppContext::sharedInstance()->tryToConnectToNetworkDevice(
                    device.macAddress);
            });
    infoLayout->addWidget(connectButton);
    infoLayout->addSpacing(5);

    // Status indicator
    QLabel *statusIndicator = new QLabel("●");
    QFont statusFont = statusIndicator->font();
    statusFont.setPointSize(12);
    statusIndicator->setFont(statusFont);
    QPalette statusPalette = statusIndicator->palette();
    statusPalette.setColor(QPalette::WindowText,
                           QColor(52, 199, 89)); // iOS green
    statusIndicator->setPalette(statusPalette);

    infoLayout->addWidget(statusIndicator);

    cardLayout->addWidget(nameLabel);
    cardLayout->addWidget(infoContainer);

    // Store the device info as property for later removal
    card->setProperty("deviceName", device.name);
    card->setProperty("deviceAddress", device.address);

    // Insert before the stretch
    m_deviceLayout->insertWidget(m_deviceLayout->count() - 1, card);
    m_deviceCards.append(card);
}

void NetworkDevicesToConnectWidget::clearDeviceCards()
{
    for (QWidget *card : m_deviceCards) {
        card->deleteLater();
    }
    m_deviceCards.clear();
}

void NetworkDevicesToConnectWidget::updateDeviceList()
{
    clearDeviceCards();

    QList<NetworkDevice> devices = m_networkProvider->getNetworkDevices();

    if (devices.isEmpty()) {
        m_statusLabel->setText("No network devices found");
    } else {
        m_statusLabel->setText(
            QString("Found %1 network device(s)").arg(devices.count()));

        for (const NetworkDevice &device : devices) {
            createDeviceCard(device);
        }
    }
}

void NetworkDevicesToConnectWidget::onWirelessDeviceAdded(
    const NetworkDevice &device)
{
    createDeviceCard(device);

    // Update status
    int deviceCount = m_deviceCards.count();
    m_statusLabel->setText(
        QString("Found %1 network device(s)").arg(deviceCount));
}

void NetworkDevicesToConnectWidget::onWirelessDeviceRemoved(
    const QString &deviceName)
{
    // Find and remove the corresponding card
    for (int i = 0; i < m_deviceCards.count(); ++i) {
        QWidget *card = m_deviceCards[i];
        if (card->property("deviceName").toString() == deviceName) {
            m_deviceCards.removeAt(i);
            card->deleteLater();
            break;
        }
    }

    // Update status
    int deviceCount = m_deviceCards.count();
    if (deviceCount == 0) {
        m_statusLabel->setText("No network devices found");
    } else {
        m_statusLabel->setText(
            QString("Found %1 network device(s)").arg(deviceCount));
    }
}