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

#include "appcontext.h"
#include <QApplication>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QPalette>
#include <QPushButton>
#include <QTimer>

NetworkDeviceCard::NetworkDeviceCard(const NetworkDevice &device,
                                     QWidget *parent)
    : QWidget(parent), m_device(device)
{
    QVBoxLayout *cardLayout = new QVBoxLayout(this);
    cardLayout->setContentsMargins(12, 10, 12, 10);
    cardLayout->setSpacing(4);

    // Device name
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

    m_connectButton = new QPushButton("Connect");
    m_connectButton->setDefault(true);
    connect(m_connectButton, &QPushButton::clicked, this, [this, device]() {
        m_connectButton->setText("Connecting...");
        m_connectButton->setEnabled(false);
        AppContext::sharedInstance()->tryToConnectToNetworkDevice(device);
    });
    infoLayout->addWidget(m_connectButton);
    infoLayout->addSpacing(5);

    QLabel *statusIndicator = new QLabel("●");
    statusIndicator->setStyleSheet(
        QString("QLabel { font-size: 14px; color: %1; }")
#ifdef WIN32
            .arg(COLOR_ACCENT_BLUE.name()));
#else
            .arg(COLOR_GREEN.name()));
#endif

    infoLayout->addWidget(statusIndicator);

    cardLayout->addWidget(nameLabel);
    cardLayout->addWidget(infoContainer);
}

void NetworkDeviceCard::failed()
{
    m_connectButton->setText("Failed to connect");
    m_connectButton->setEnabled(false);

    QTimer::singleShot(2000, this, [this]() {
        m_connectButton->setText("Connect");
        m_connectButton->setEnabled(true);
    });
}

void NetworkDeviceCard::noPairingFile()
{
    // TODO: add a button or hint to explain how to create a pairing file for
    // this device
    m_connectButton->setText("No pairing file");
    m_connectButton->setEnabled(false);

    QTimer::singleShot(5000, this, [this]() {
        m_connectButton->setText("Connect");
        m_connectButton->setEnabled(true);
    });
}

void NetworkDeviceCard::connected()
{
    m_connectButton->setText("Connected");
    m_connectButton->setEnabled(false);

    QTimer::singleShot(10000, this, [this]() {
        m_connectButton->setText("Connect");
        m_connectButton->setEnabled(true);
    });
}

void NetworkDeviceCard::alreadyExists()
{
    m_connectButton->setText("Already connected");
    m_connectButton->setEnabled(false);

    QTimer::singleShot(3000, this, [this]() {
        m_connectButton->setText("Connect");
        m_connectButton->setEnabled(true);
    });
}

void NetworkDeviceCard::initStarted()
{
    m_connectButton->setText("Connecting...");
    m_connectButton->setEnabled(false);
}

NetworkDevicesToConnectWidget::NetworkDevicesToConnectWidget(QWidget *parent)
    : QWidget(parent)
{
    setupUI();

    connect(NetworkDeviceProvider::sharedInstance(),
            &NetworkDeviceProvider::deviceAdded, this,
            &NetworkDevicesToConnectWidget::onWirelessDeviceAdded);
    connect(NetworkDeviceProvider::sharedInstance(),
            &NetworkDeviceProvider::deviceRemoved, this,
            &NetworkDevicesToConnectWidget::onWirelessDeviceRemoved);

    updateDeviceList();

    // in case the backend fails to find pairing file
    connect(AppContext::sharedInstance()->core, &CXX::Core::no_pairing_file,
            this,
            &NetworkDevicesToConnectWidget::onNoPairingFileForWirelessDevice);

    connect(AppContext::sharedInstance(),
            &AppContext::noPairingFileForWirelessDevice, this,
            &NetworkDevicesToConnectWidget::onNoPairingFileForWirelessDevice);

    connect(AppContext::sharedInstance()->core, &CXX::Core::init_failed, this,
            &NetworkDevicesToConnectWidget::onDeviceInitFailed);
    connect(AppContext::sharedInstance(), &AppContext::initStarted, this,
            &NetworkDevicesToConnectWidget::onDeviceInitStarted);
    connect(AppContext::sharedInstance(), &AppContext::deviceAdded, this,
            &NetworkDevicesToConnectWidget::onDeviceAdded);
    connect(AppContext::sharedInstance(), &AppContext::deviceAlreadyExistsMAC,
            this, &NetworkDevicesToConnectWidget::onDeviceAlreadyExists);

    // eval interval
    QTimer *evalTimer = new QTimer(this);
    connect(evalTimer, &QTimer::timeout, this,
            &NetworkDevicesToConnectWidget::eval);
    evalTimer->start(30000); // evaluate every 30 seconds
}

NetworkDevicesToConnectWidget::~NetworkDevicesToConnectWidget() {}

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
    m_scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_scrollArea->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    m_scrollArea->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
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
    groupLayout->addWidget(m_scrollArea, 1);

    mainLayout->addWidget(m_deviceGroup, 1);
}

void NetworkDevicesToConnectWidget::createDeviceCard(
    const NetworkDevice &device)
{
    NetworkDeviceCard *card = new NetworkDeviceCard(device);

    m_deviceLayout->insertWidget(m_deviceLayout->count() - 1, card);
    m_deviceCards[device.macAddress] = card;
}

void NetworkDevicesToConnectWidget::clearDeviceCards()
{
    for (QWidget *card : m_deviceCards) {
        if (card) {
            card->deleteLater();
        }
    }
    m_deviceCards.clear();
}
void NetworkDevicesToConnectWidget::updateDeviceList()
{
    clearDeviceCards();

    QList<NetworkDevice> devices =
        NetworkDeviceProvider::sharedInstance()->getNetworkDevices();

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
    if (m_deviceCards.contains(device.macAddress)) {
        qDebug() << "Device with MAC" << device.macAddress
                 << "already exists in the list. Skipping addition.";
        return;
    }
    createDeviceCard(device);

    // Update status
    int deviceCount = m_deviceCards.count();
    m_statusLabel->setText(
        QString("Found %1 network device(s)").arg(deviceCount));
}

void NetworkDevicesToConnectWidget::onWirelessDeviceRemoved(
    const QString &macAddress)
{
    // Find and remove the corresponding card
    NetworkDeviceCard *card = m_deviceCards[macAddress];
    m_deviceCards.remove(macAddress);
    if (card) {
        card->deleteLater();
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

void NetworkDevicesToConnectWidget::eval()
{
    if (QCoreApplication::closingDown())
        return;
    bool forceCache = true;
    for (const auto &card : m_deviceCards) {
        if (card) {
            NetworkDevice device = card->getDevice();
            AppContext::sharedInstance()->tryToConnectToNetworkDevice(
                device, forceCache, false);
            forceCache = false; // force cache once
        }
    }
}

void NetworkDevicesToConnectWidget::onNoPairingFileForWirelessDevice(
    const QString &macAddress)
{
    NetworkDeviceCard *deviceCard = m_deviceCards[macAddress];
    if (deviceCard) {
        qDebug() << "Calling noPairingFile() on device card for" << macAddress;
        deviceCard->noPairingFile();
    }
}

// udid or mac address
void NetworkDevicesToConnectWidget::onDeviceInitFailed(const QString &uniq)
{
    NetworkDeviceCard *deviceCard = m_deviceCards[uniq];
    if (deviceCard) {
        qDebug() << "Calling failed() on device card for" << uniq;
        deviceCard->failed();
    }
}

void NetworkDevicesToConnectWidget::onDeviceInitStarted(const QString &uniq)
{
    NetworkDeviceCard *deviceCard = m_deviceCards[uniq];
    if (deviceCard) {
        qDebug() << "Calling initStarted() on device card for" << uniq;
        deviceCard->initStarted();
    }
}

void NetworkDevicesToConnectWidget::onDeviceAdded(
    const std::shared_ptr<iDescriptorDevice> device)
{
    NetworkDeviceCard *deviceCard = m_deviceCards[QString::fromStdString(
        device->deviceInfo.wifiMacAddress)];
    if (deviceCard) {
        qDebug() << "Calling connected() on device card for"
                 << QString::fromStdString(device->deviceInfo.wifiMacAddress);
        deviceCard->connected();
        return;
    }
    qDebug() << "No device card found for"
             << QString::fromStdString(device->deviceInfo.wifiMacAddress);
}

void NetworkDevicesToConnectWidget::onDeviceAlreadyExists(
    const iDescriptor::Uniq &uniq)
{
    NetworkDeviceCard *deviceCard = m_deviceCards[QString(uniq.get())];
    if (deviceCard) {
        qDebug() << "Calling alreadyExists() on device card for" << uniq.get();
        deviceCard->alreadyExists();
        return;
    }
    qDebug() << "No device card found for" << uniq.get();
}
