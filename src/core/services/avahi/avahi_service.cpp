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

#include "avahi_service.h"
#include <QDebug>
#include <QMutexLocker>
#include <avahi-common/error.h>
#include <avahi-common/malloc.h>

AvahiService::AvahiService(QObject *parent)
    : QObject(parent), m_simplePoll(nullptr), m_client(nullptr),
      m_serviceBrowser(nullptr), m_pollTimer(new QTimer(this)), m_running(false)
{
    connect(m_pollTimer, &QTimer::timeout, this, &AvahiService::pollAvahi);
}

AvahiService::~AvahiService() { stopBrowsing(); }

void AvahiService::startBrowsing()
{
    if (m_running)
        return;

    qDebug() << "Starting Avahi browsing for Apple devices";
    initializeAvahi();

    if (m_simplePoll) {
        m_running = true;
        m_pollTimer->start(100); // Poll every 100ms
    }
}

void AvahiService::stopBrowsing()
{
    if (!m_running)
        return;

    qDebug() << "Stopping Avahi browsing";
    m_running = false;
    m_pollTimer->stop();
    cleanupAvahi();

    QMutexLocker locker(&m_devicesMutex);
    m_networkDevices.clear();
}

QList<NetworkDevice> AvahiService::getNetworkDevices() const
{
    QMutexLocker locker(&m_devicesMutex);
    return m_networkDevices;
}

void AvahiService::pollAvahi()
{
    if (m_simplePoll && m_running) {
        avahi_simple_poll_iterate(m_simplePoll, 0); // Non-blocking
    }
}

void AvahiService::initializeAvahi()
{
    int error;

    m_simplePoll = avahi_simple_poll_new();
    if (!m_simplePoll) {
        qWarning() << "Failed to create Avahi simple poll";
        return;
    }

    m_client =
        avahi_client_new(avahi_simple_poll_get(m_simplePoll),
                         (AvahiClientFlags)0, clientCallback, this, &error);
    if (!m_client) {
        qWarning() << "Failed to create Avahi client:" << avahi_strerror(error);
        cleanupAvahi();
        return;
    }
}

void AvahiService::cleanupAvahi()
{
    if (m_serviceBrowser) {
        avahi_service_browser_free(m_serviceBrowser);
        m_serviceBrowser = nullptr;
    }

    if (m_client) {
        avahi_client_free(m_client);
        m_client = nullptr;
    }

    if (m_simplePoll) {
        avahi_simple_poll_free(m_simplePoll);
        m_simplePoll = nullptr;
    }
}

void AvahiService::clientCallback(AvahiClient *client, AvahiClientState state,
                                  void *userdata)
{
    AvahiService *service = static_cast<AvahiService *>(userdata);

    if (state == AVAHI_CLIENT_S_RUNNING) {
        qDebug() << "Avahi client running, creating service browser";
        service->m_serviceBrowser = avahi_service_browser_new(
            client, AVAHI_IF_UNSPEC, AVAHI_PROTO_UNSPEC, "_apple-mobdev2._tcp",
            nullptr, (AvahiLookupFlags)0, browseCallback, userdata);

        if (!service->m_serviceBrowser) {
            qWarning() << "Failed to create service browser:"
                       << avahi_strerror(avahi_client_errno(client));
        }
    } else if (state == AVAHI_CLIENT_FAILURE) {
        qWarning() << "Avahi client failure:"
                   << avahi_strerror(avahi_client_errno(client));
        service->m_running = false;
    }
}

void AvahiService::browseCallback(AvahiServiceBrowser *browser,
                                  AvahiIfIndex interface,
                                  AvahiProtocol protocol,
                                  AvahiBrowserEvent event, const char *name,
                                  const char *type, const char *domain,
                                  AvahiLookupResultFlags flags, void *userdata)
{
    Q_UNUSED(browser)
    Q_UNUSED(flags)

    AvahiService *service = static_cast<AvahiService *>(userdata);
    QString macAddress = QString::fromUtf8(name).split('@').first();

    switch (event) {
    case AVAHI_BROWSER_NEW:
        if (!avahi_service_resolver_new(service->m_client, interface, protocol,
                                        name, type, domain, AVAHI_PROTO_UNSPEC,
                                        (AvahiLookupFlags)0, resolveCallback,
                                        userdata)) {
            qWarning() << "Failed to create resolver for" << name;
        }
        break;

    case AVAHI_BROWSER_REMOVE:
        qDebug() << "Apple device removed:" << name;
        emit service->deviceRemoved(macAddress);

        // Remove from our list
        {
            QMutexLocker locker(&service->m_devicesMutex);
            service->m_networkDevices.removeIf(
                [macAddress](const NetworkDevice &dev) {
                    return dev.macAddress == macAddress;
                });
        }
        break;

    case AVAHI_BROWSER_FAILURE:
        qWarning() << "Browser failure";
        break;

    default:
        break;
    }
}

void AvahiService::resolveCallback(
    AvahiServiceResolver *resolver, AvahiIfIndex interface,
    AvahiProtocol protocol, AvahiResolverEvent event, const char *name,
    const char *type, const char *domain, const char *host_name,
    const AvahiAddress *address, uint16_t port, AvahiStringList *txt,
    AvahiLookupResultFlags flags, void *userdata)
{
    Q_UNUSED(interface)
    Q_UNUSED(protocol)
    Q_UNUSED(type)
    Q_UNUSED(domain)
    Q_UNUSED(flags)

    AvahiService *service = static_cast<AvahiService *>(userdata);

    if (event == AVAHI_RESOLVER_FOUND) {
        NetworkDevice device;
        device.name = QString::fromUtf8(name);
        device.hostname = QString::fromUtf8(host_name);
        device.port = port > 0 ? port : 22; // Default to SSH port

        // Convert address to string
        char addr_str[AVAHI_ADDRESS_STR_MAX];
        avahi_address_snprint(addr_str, sizeof(addr_str), address);
        device.address = QString::fromUtf8(addr_str);

        // Parse TXT records
        for (AvahiStringList *t = txt; t; t = t->next) {
            char *key = nullptr;
            char *value = nullptr;
            avahi_string_list_get_pair(t, &key, &value, nullptr);
            if (key) {
                device.txt[key] = value ? value : "";
                avahi_free(key);
            }
            if (value) {
                avahi_free(value);
            }
        }
        device.macAddress = device.name.split('@').first();
        qDebug() << "Resolved Apple device:" << device.name << "at"
                 << device.address << ":" << device.port
                 << "MAC:" << device.macAddress;

        // Add to our list if not already present
        {
            QMutexLocker locker(&service->m_devicesMutex);
            bool exists = std::any_of(service->m_networkDevices.begin(),
                                      service->m_networkDevices.end(),
                                      [&device](const NetworkDevice &existing) {
                                          return existing == device;
                                      });
            if (!exists) {
                service->m_networkDevices.append(device);
                emit service->deviceAdded(device);
            }
        }

    } else if (event == AVAHI_RESOLVER_FAILURE) {
        qWarning() << "Failed to resolve service" << name;
    }

    avahi_service_resolver_free(resolver);
}