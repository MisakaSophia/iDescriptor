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

#ifndef APPCONTEXT_H
#define APPCONTEXT_H

#include "devicesidebarwidget.h"
#include "heartbeat.h"
#include "iDescriptor.h"
#include "mainwindow.h"
#include "settingsmanager.h"
#include <QDebug>
#include <QMessageBox>
#include <QObject>
#include <QThreadPool>
#include <QTimer>
#include <QUuid>
#include <thread>

class AppContext : public QObject
{
    Q_OBJECT
public:
    static AppContext *sharedInstance();
    iDescriptorDevice *getDevice(const std::string &udid);
    QList<iDescriptorDevice *> getAllDevices();
    explicit AppContext(QObject *parent = nullptr);
    bool noDevicesConnected() const;
    void cachePairingFile(const QString &udid, const QString &pairingFilePath);
    const QString getCachedPairingFile(const QString &udid) const;

    void tryToConnectToNetworkDevice(const NetworkDevice &device);
#ifdef ENABLE_RECOVERY_DEVICE_SUPPORT
    QList<iDescriptorRecoveryDevice *> getAllRecoveryDevices();
#endif
    ~AppContext();
    int getConnectedDeviceCount() const;

    void setCurrentDeviceSelection(const DeviceSelection &selection);
    const DeviceSelection &getCurrentDeviceSelection() const;
    const iDescriptorDevice *
    getDeviceByMacAddress(const QString &macAddress) const;

private:
    QMap<std::string, iDescriptorDevice *> m_devices;
#ifdef ENABLE_RECOVERY_DEVICE_SUPPORT
    QMap<uint64_t, iDescriptorRecoveryDevice *> m_recoveryDevices;
#endif
    QStringList m_pendingDevices;
    DeviceSelection m_currentSelection = DeviceSelection("");
    QMap<QString, QString> m_pairingFileCache;
    void cachePairedDevices();
    void emitNoPairingFileForWirelessDevice(const QString &udid);
    void freeDevice(iDescriptorDevice *device);
    void handlePairing(iDescriptorInitDeviceResult *initResult, AddType addType,
                       iDescriptor::Uniq uniq);
signals:
    void deviceAdded(const iDescriptorDevice *device);
    void deviceRemoved(const std::string &udid, const std::string &macAddress,
                       const std::string &ipAddress, bool wasWireless);
    void devicePaired(const iDescriptorDevice *device);
    void devicePasswordProtected(const QString &udid);
    void deviceAlreadyExists(const iDescriptor::Uniq &uniq);
#ifdef ENABLE_RECOVERY_DEVICE_SUPPORT
    void recoveryDeviceAdded(const iDescriptorRecoveryDevice *deviceInfo);
    void recoveryDeviceRemoved(uint64_t ecid);
#endif
    void devicePairPending(const QString &udid);
    void devicePairingExpired(const QString &udid);
    // only fired on wireless devices when we have no pairing file for them
    void noPairingFileForWirelessDevice(const QString &macAddress);
    void initFailed(const QString &udid);
    void initStarted(const QString &udid);

    void systemSleepStarting();
    void systemWakeup();
    /*
        Generic change event for any device state change we
        need this because many UI elements need to update by
        listening for this only you can watch for any event
        and using the public members of this class you can
        do anything you want
    */
    void deviceChange();
    void currentDeviceSelectionChanged(const DeviceSelection &selection);
    void deviceHeartbeatFailed(const QString &macAddress, int tries);
public slots:
    void removeDevice(iDescriptor::Uniq uniq);
    void addDevice(iDescriptor::Uniq udid,
                   iDescriptor::IdeviceConnectionType connType, AddType addType,
                   QString wifiMacAddress = QString(),
                   QString ipAddress = QString());
    void heartbeatFailed(const QString &macAddress, int tries);
    // void heartbeatThreadExited(const QString &macAddress);
#ifdef ENABLE_RECOVERY_DEVICE_SUPPORT
    void addRecoveryDevice(uint64_t ecid);
    void removeRecoveryDevice(uint64_t ecid);
#endif
};

#endif // APPCONTEXT_H
