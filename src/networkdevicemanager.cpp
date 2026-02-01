#include "networkdevicemanager.h"
#include <QTimer>

NetworkDeviceManager *NetworkDeviceManager::sharedInstance()
{
    static NetworkDeviceManager instance;
    return &instance;
}

NetworkDeviceManager::NetworkDeviceManager(QObject *parent) : QObject{parent}
{

#ifdef __linux__
    m_networkProvider = new AvahiService(this);
    connect(m_networkProvider, &AvahiService::deviceAdded, this,
            &NetworkDeviceManager::deviceAdded);
    connect(m_networkProvider, &AvahiService::deviceRemoved, this,
            &NetworkDeviceManager::deviceRemoved);
#else
    m_networkProvider = new DnssdService(this);
    connect(m_networkProvider, &DnssdService::deviceAdded, this,
            &NetworkDeviceManager::deviceAdded);
    connect(m_networkProvider, &DnssdService::deviceRemoved, this,
            &NetworkDeviceManager::deviceRemoved);
#endif

    /* Helps main ui load a litte faster */
    QTimer::singleShot(std::chrono::seconds(1), this,
                       [this]() { m_networkProvider->startBrowsing(); });
}
