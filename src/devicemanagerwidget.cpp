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

#include "devicemanagerwidget.h"

DeviceManagerWidget::DeviceManagerWidget(QWidget *parent)
    : QWidget(parent), m_currentDeviceUuid("")
{
    setupUI();

    connect(
        AppContext::sharedInstance(), &AppContext::deviceAdded, this,
        [this](const std::shared_ptr<iDescriptorDevice> device) {
            addDevice(device);

            SettingsManager::sharedInstance()->doIfEnabled(
                SettingsManager::Setting::AutoRaiseWindow, []() {
                    if (MainWindow *mainWindow = MainWindow::sharedInstance()) {
                        mainWindow->raiseDeviceTab();
                    }
                });

            SettingsManager::sharedInstance()->doIfEnabled(
                SettingsManager::Setting::SwitchToNewDevice, [this, device]() {
                    AppContext::sharedInstance()->setCurrentDeviceSelection(
                        DeviceSelection(device->udid));
                });

            updateUI();
        });

    connect(AppContext::sharedInstance(), &AppContext::deviceRemoved, this,
            [this](const QString &uuid) {
                removeDevice(uuid);
                auto devices = AppContext::sharedInstance()->getAllDevices();
                if (!devices.isEmpty())
                    AppContext::sharedInstance()->setCurrentDeviceSelection(
                        DeviceSelection(devices.first()->udid));
                updateUI();
            });

    connect(AppContext::sharedInstance(), &AppContext::devicePairPending, this,
            [this](const QString &udid) {
                addPendingDevice(udid, false);
                updateUI();
            });

    connect(AppContext::sharedInstance(), &AppContext::devicePasswordProtected,
            this, [this](const QString &udid) {
                addPendingDevice(udid, true);
                updateUI();
            });

    connect(AppContext::sharedInstance(), &AppContext::devicePaired, this,
            [this](const std::shared_ptr<iDescriptorDevice> device) {
                addPairedDevice(device);
                SettingsManager::sharedInstance()->doIfEnabled(
                    SettingsManager::Setting::SwitchToNewDevice,
                    [this, device]() {
                        AppContext::sharedInstance()->setCurrentDeviceSelection(
                            DeviceSelection(device->udid));
                    });

                updateUI();
            });

#ifdef ENABLE_RECOVERY_DEVICE_SUPPORT
    connect(AppContext::sharedInstance(), &AppContext::recoveryDeviceAdded,
            this, [this](const iDescriptorRecoveryDevice *recoveryDeviceInfo) {
                addRecoveryDevice(recoveryDeviceInfo);
                updateUI();
            });

    connect(AppContext::sharedInstance(), &AppContext::recoveryDeviceRemoved,
            this, [this](uint64_t ecid) {
                removeRecoveryDevice(ecid);
                updateUI();
            });
#endif

    connect(AppContext::sharedInstance(), &AppContext::devicePairingExpired,
            this, [this](const QString &udid) {
                removePendingDevice(udid);
                updateUI();
            });
    onDeviceSelectionChanged(
        AppContext::sharedInstance()->getCurrentDeviceSelection());
}

void DeviceManagerWidget::setupUI()
{
    m_mainLayout = new QHBoxLayout(this);
    m_mainLayout->setContentsMargins(0, 0, 0, 0);
    m_mainLayout->setSpacing(0);

    m_noDevicesLabel = new QLabel("This is where devices will appear", this);
    m_noDevicesLabel->setFont(QFont("", 20, QFont::Bold));
    m_noDevicesLabel->setAlignment(Qt::AlignCenter);
    m_noDevicesLabel->setWordWrap(true);

    // Create sidebar
    m_sidebar = new DeviceSidebarWidget();

    // Create stacked widget for device content
    m_stackedWidget = new QStackedWidget();

    // Add to layout
    m_mainLayout->addWidget(m_sidebar);
    m_mainLayout->addWidget(m_stackedWidget);

    // Connect signals
    connect(AppContext::sharedInstance(),
            &AppContext::currentDeviceSelectionChanged, this,
            &DeviceManagerWidget::onDeviceSelectionChanged);
}

void DeviceManagerWidget::addDevice(
    const std::shared_ptr<iDescriptorDevice> device)
{
    if (m_deviceWidgets.contains(device->udid)) {
        qWarning() << "Device already exists:" << device->udid;
        return;
    }
    qDebug() << "Connect ::deviceAdded Adding:" << device->udid;
    DeviceMenuWidget *deviceWidget = new DeviceMenuWidget(device, this);

    QString tabTitle = QString::fromStdString(device->deviceInfo.productType);

    m_stackedWidget->addWidget(deviceWidget);
    m_deviceWidgets[device->udid] = std::pair{
        deviceWidget, m_sidebar->addDevice(tabTitle, device->udid,
                                           device->deviceInfo.isWireless)};
}

#ifdef ENABLE_RECOVERY_DEVICE_SUPPORT
void DeviceManagerWidget::addRecoveryDevice(
    const iDescriptorRecoveryDevice *device)
{
    try {
        // Create device info widget
        RecoveryDeviceInfoWidget *recoveryDeviceInfoWidget =
            new RecoveryDeviceInfoWidget(device);
        m_recoveryDeviceWidgets.insert(
            device->ecid,
            std::pair{recoveryDeviceInfoWidget,
                      m_sidebar->addRecoveryDevice(device->ecid)});
        m_stackedWidget->addWidget(recoveryDeviceInfoWidget);

    } catch (...) {
        qDebug() << "Error initializing recovery device";
    }
}

void DeviceManagerWidget::removeRecoveryDevice(uint64_t ecid)
{
    qDebug() << "Removing recovery device with ECID:" << ecid;
    if (!m_recoveryDeviceWidgets.contains(ecid)) {
        qDebug() << "Recovery device with ECID" + QString::number(ecid) +
                        " not found. Please report this issue.";
        return;
    }

    RecoveryDeviceInfoWidget *deviceWidget =
        m_recoveryDeviceWidgets[ecid].first;
    RecoveryDeviceSidebarItem *sidebarItem =
        m_recoveryDeviceWidgets[ecid].second;

    if (deviceWidget != nullptr && sidebarItem != nullptr) {
        qDebug() << "Recovery device exists removing:" << QString::number(ecid);

        m_recoveryDeviceWidgets.remove(ecid);
        m_stackedWidget->removeWidget(deviceWidget);
        m_sidebar->removeRecoveryDevice(ecid);
        deviceWidget->deleteLater();

        emit updateNoDevicesConnected();
    }
}
#endif

void DeviceManagerWidget::addPendingDevice(const QString &uniq, bool locked)
{
    qDebug() << "Adding pending device:" << uniq;
    if (m_pendingDeviceWidgets.contains(uniq) && !locked) {
        qDebug() << "Pending device already exists, moving to next state:"
                 << uniq;
        m_pendingDeviceWidgets[uniq].first->next();
        return;
    } else if (m_pendingDeviceWidgets.contains(uniq) && locked) {
        // Already exists and still locked, do nothing
        qDebug()
            << "Pending device already exists and is locked, doing nothing:"
            << uniq;
        return;
    }

    qDebug() << "Created pending widget for:" << uniq << "Locked:" << locked;
    DevicePendingWidget *pendingWidget = new DevicePendingWidget(locked, this);
    m_stackedWidget->addWidget(pendingWidget);
    m_pendingDeviceWidgets[uniq] =
        std::pair{pendingWidget, m_sidebar->addPendingDevice(uniq)};
}

void DeviceManagerWidget::removePendingDevice(const QString &udid)
{
    qDebug() << "Removing pending device:" << udid;
    if (!m_pendingDeviceWidgets.contains(udid)) {
        qDebug() << "Pending device not found:" << udid;
        return;
    }
    DevicePendingWidget *deviceWidget = m_pendingDeviceWidgets[udid].first;
    DevicePendingSidebarItem *sidebarItem = m_pendingDeviceWidgets[udid].second;

    if (deviceWidget != nullptr && sidebarItem != nullptr) {
        qDebug() << "Pending device exists removing:" << udid;
        m_pendingDeviceWidgets.remove(udid);
        m_stackedWidget->removeWidget(deviceWidget);
        m_sidebar->removePendingDevice(udid);
        deviceWidget->deleteLater();
    }
}

void DeviceManagerWidget::addPairedDevice(
    const std::shared_ptr<iDescriptorDevice> device)
{
    qDebug() << "Device paired:" << device->udid;

    // Check if pending device exists
    if (m_pendingDeviceWidgets.contains(device->udid)) {
        std::pair<DevicePendingWidget *, DevicePendingSidebarItem *> &pair =
            m_pendingDeviceWidgets[device->udid];

        // Remove from sidebar if it exists
        if (pair.second) {
            qDebug() << "Removing pending device from sidebar:" << device->udid;
            m_sidebar->removePendingDevice(device->udid);
        }

        // Clean up widget if it exists
        if (pair.first) {
            qDebug() << "Removing pending device widget:" << device->udid;
            m_stackedWidget->removeWidget(pair.first);
            pair.first->deleteLater();
        }

        m_pendingDeviceWidgets.remove(device->udid);
    }

    addDevice(device);
}

void DeviceManagerWidget::removeDevice(const QString &uuid)
{

    qDebug() << "Removing:" << uuid;
    DeviceMenuWidget *deviceWidget = m_deviceWidgets[uuid].first;
    DeviceSidebarItem *sidebarItem = m_deviceWidgets[uuid].second;

    if (deviceWidget != nullptr && sidebarItem != nullptr) {
        qDebug() << "Device exists removing:" << uuid;
        // FIXME: cleanups
        m_deviceWidgets.remove(uuid);
        m_stackedWidget->removeWidget(deviceWidget);
        m_sidebar->removeDevice(uuid);
        deviceWidget->deleteLater();
    }
}

void DeviceManagerWidget::setCurrentDevice(const QString &uuid)
{
    qDebug() << "Setting current device to:" << uuid;
    if (m_currentDeviceUuid == uuid)
        return;

    if (!m_deviceWidgets.contains(uuid)) {
        qWarning() << "Device UUID not found:" << uuid;
        return;
    }

    m_currentDeviceUuid = uuid;

    QWidget *widget = m_deviceWidgets[uuid].first;
    m_stackedWidget->setCurrentWidget(widget);
}

QString DeviceManagerWidget::getCurrentDevice() const
{
    return m_currentDeviceUuid;
}

void DeviceManagerWidget::onDeviceSelectionChanged(
    const DeviceSelection &selection)
{
    // Update sidebar selection
    m_sidebar->setCurrentSelection(selection);

    switch (selection.type) {
    case DeviceSelection::Normal:
        if (m_deviceWidgets.contains(selection.udid)) {
            if (m_currentDeviceUuid != selection.udid) {
                setCurrentDevice(selection.udid);
            }

            // Handle navigation section
            QWidget *tabWidget = m_deviceWidgets[selection.udid].first;
            DeviceMenuWidget *deviceMenuWidget =
                qobject_cast<DeviceMenuWidget *>(tabWidget);
            qDebug() << "Switching to tab:" << selection.section
                     << deviceMenuWidget;
            if (deviceMenuWidget && !selection.section.isEmpty()) {
                deviceMenuWidget->switchToTab(selection.section);
            }
        }
        break;

#ifdef ENABLE_RECOVERY_DEVICE_SUPPORT
    case DeviceSelection::Recovery:
        if (m_recoveryDeviceWidgets.contains(selection.ecid)) {
            QWidget *tabWidget = m_recoveryDeviceWidgets[selection.ecid].first;
            if (tabWidget) {
                m_stackedWidget->setCurrentWidget(tabWidget);
                // Clear current device since we're viewing recovery device
                m_currentDeviceUuid = "";
            }
        }
        break;
#endif

    case DeviceSelection::Pending:
        if (m_pendingDeviceWidgets.contains(selection.udid)) {
            QWidget *tabWidget = m_pendingDeviceWidgets[selection.udid].first;
            if (tabWidget) {
                m_stackedWidget->setCurrentWidget(tabWidget);
                // Clear current device since we're viewing pending device
                m_currentDeviceUuid = "";
            }
        }
        break;
    }
}

void DeviceManagerWidget::updateUI()
{
    emit updateNoDevicesConnected();
    m_noDevicesLabel->setVisible(
        AppContext::sharedInstance()->noDevicesConnected());
}

void DeviceManagerWidget::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);

    if (!m_noDevicesLabel)
        return;

    const int margin = 10;
    int maxWidth = qMax(0, width() - 2 * margin);
    m_noDevicesLabel->setMaximumWidth(maxWidth);
    m_noDevicesLabel->adjustSize();

    int x = (width() - m_noDevicesLabel->width()) / 2;
    int y = (height() - m_noDevicesLabel->height()) / 2;
    x = qMax(margin, x);
    y = qMax(margin, y);

    m_noDevicesLabel->move(x, y);
}
