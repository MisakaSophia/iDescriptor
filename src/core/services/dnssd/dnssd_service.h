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

#ifndef DNSSD_SERVICE_H
#define DNSSD_SERVICE_H

#include "../../../iDescriptor.h"
#include <QList>
#include <QMap>
#include <QMutex>
#include <QObject>
#include <QSocketNotifier>
#include <QString>
#include <map>
#include <string>

#ifdef WIN32
#include "dns_sd.h"
#else
#include <dns_sd.h>
#endif

class DnssdService : public QObject
{
    Q_OBJECT

public:
    explicit DnssdService(QObject *parent = nullptr);
    ~DnssdService();

    void startBrowsing();
    void stopBrowsing();
    QMap<QString, NetworkDevice> getNetworkDevices() const;
    NetworkDevice getNetworkDeviceByMac(const QString &macAddress) const;

signals:
    void deviceAdded(const NetworkDevice &device);
    void deviceRemoved(const QString &macAddress);

private slots:
    void processDnssdEvents();

private:
    void cleanupDnssd();

    static void DNSSD_API browseCallback(
        DNSServiceRef sdRef, DNSServiceFlags flags, uint32_t interfaceIndex,
        DNSServiceErrorType errorCode, const char *serviceName,
        const char *regtype, const char *replyDomain, void *context);

    static void DNSSD_API resolveCallback(
        DNSServiceRef sdRef, DNSServiceFlags flags, uint32_t interfaceIndex,
        DNSServiceErrorType errorCode, const char *fullname,
        const char *hosttarget, uint16_t port, uint16_t txtLen,
        const unsigned char *txtRecord, void *context);

    static void DNSSD_API addrInfoCallback(
        DNSServiceRef sdRef, DNSServiceFlags flags, uint32_t interfaceIndex,
        DNSServiceErrorType errorCode, const char *hostname,
        const struct sockaddr *address, uint32_t ttl, void *context);

    DNSServiceRef m_browseRef;
    QSocketNotifier *m_socketNotifier;

    mutable QMutex m_devicesMutex;
    QMap<QString, NetworkDevice> m_networkDevices;
    bool m_running;

    // Temporary storage for devices being resolved
    struct PendingDevice {
        QString name;
        QString hostname;
        uint16_t port;
        uint32_t interfaceIndex;
        QMap<QString, QString> txt;
        QString macAddress;
    };
    QMap<QString, PendingDevice> m_pendingDevices;
};

#endif // DNSSD_SERVICE_H
