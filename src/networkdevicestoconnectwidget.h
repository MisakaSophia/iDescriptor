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

#ifndef NETWORKDEVICESTOCONNECTWIDGET_H
#define NETWORKDEVICESTOCONNECTWIDGET_H

#include "iDescriptor-ui.h"
#include "networkdeviceprovider.h"

#include <QGroupBox>
#include <QLabel>
#include <QPushButton>
#include <QScrollArea>
#include <QVBoxLayout>
#include <QWidget>

class NetworkDeviceCard : public QWidget
{
    Q_OBJECT
public:
    explicit NetworkDeviceCard(const NetworkDevice &device,
                               QWidget *parent = nullptr);

private:
    QPushButton *m_connectButton = nullptr;
    NetworkDevice m_device;

public:
    void failed();
    void noPairingFile();
    void initStarted();
    void connected();
    void alreadyExists();
    NetworkDevice getDevice() { return m_device; };
};

class NetworkDevicesToConnectWidget : public QWidget
{
    Q_OBJECT

public:
    explicit NetworkDevicesToConnectWidget(QWidget *parent = nullptr);
    ~NetworkDevicesToConnectWidget();

private slots:
    void onWirelessDeviceAdded(const NetworkDevice &device);
    void onWirelessDeviceRemoved(const QString &deviceName);
    void onNoPairingFileForWirelessDevice(const QString &macAddress);
    void onDeviceInitFailed(const QString &udid);
    void onDeviceInitStarted(const QString &udid);
    void onDeviceAdded(const std::shared_ptr<iDescriptorDevice> device);
    void onDeviceAlreadyExists(const iDescriptor::Uniq &uniq);

private:
    void setupUI();
    void createDeviceCard(const NetworkDevice &device);
    void clearDeviceCards();
    void updateDeviceList();
    void eval();
    QGroupBox *m_deviceGroup = nullptr;
    QScrollArea *m_scrollArea = nullptr;
    QWidget *m_scrollContent = nullptr;
    QVBoxLayout *m_deviceLayout = nullptr;
    QLabel *m_statusLabel = nullptr;

    QMap<QString, NetworkDeviceCard *> m_deviceCards;
};

#endif // NETWORKDEVICESTOCONNECTWIDGET_H