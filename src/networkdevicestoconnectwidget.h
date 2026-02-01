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

#ifdef __linux__
#include "core/services/avahi/avahi_service.h"
#else
#include "core/services/dnssd/dnssd_service.h"
#endif

#include <QGroupBox>
#include <QLabel>
#include <QScrollArea>
#include <QVBoxLayout>
#include <QWidget>

class NetworkDevicesToConnectWidget : public QWidget
{
    Q_OBJECT

public:
    explicit NetworkDevicesToConnectWidget(QWidget *parent = nullptr);
    ~NetworkDevicesToConnectWidget();

private slots:
    void onWirelessDeviceAdded(const NetworkDevice &device);
    void onWirelessDeviceRemoved(const QString &deviceName);

private:
    void setupUI();
    void createDeviceCard(const NetworkDevice &device);
    void clearDeviceCards();
    void updateDeviceList();

    QGroupBox *m_deviceGroup = nullptr;
    QScrollArea *m_scrollArea = nullptr;
    QWidget *m_scrollContent = nullptr;
    QVBoxLayout *m_deviceLayout = nullptr;
    QLabel *m_statusLabel = nullptr;

#ifdef __linux__
    AvahiService *m_networkProvider = nullptr;
#else
    DnssdService *m_networkProvider = nullptr;
#endif

    QList<QWidget *> m_deviceCards;
};

#endif // NETWORKDEVICESTOCONNECTWIDGET_H