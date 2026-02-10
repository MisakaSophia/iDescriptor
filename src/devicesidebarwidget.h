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

#ifndef DEVICESIDEBARWIDGET_H
#define DEVICESIDEBARWIDGET_H

#include "iDescriptor-ui.h"
#include <QButtonGroup>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QScrollArea>
#include <QVBoxLayout>
#include <QWidget>

class DeviceSidebarItem : public QFrame
{
    Q_OBJECT

public:
    explicit DeviceSidebarItem(const QString &deviceName,
                               const std::string &uuid, bool isWireless,
                               QWidget *parent = nullptr);
    const std::string &getDeviceUuid() const;

    void setSelected(bool selected);
    bool isSelected() const { return m_selected; }
    void setCollapsed(bool collapsed);
    bool isCollapsed() const { return m_collapsed; }

protected:
    void paintEvent(QPaintEvent *event) override;
signals:
    void deviceSelected(const std::string &uuid);
    void navigationRequested(const std::string &uuid, const QString &section);

private slots:
    void onToggleCollapse();
    void onNavigationButtonClicked();

private:
    void setupUI();
    void updateToggleButton();
    void toggleCollapse();

    std::string m_uuid;
    QString m_deviceName;
    bool m_selected;
    bool m_collapsed;
    QVBoxLayout *m_mainLayout;
    ClickableWidget *m_headerWidget;
    QWidget *m_optionsWidget;
    QPushButton *m_toggleButton;
    QLabel *m_deviceLabel;
    bool m_wireless;

    // Navigation buttons
    QPushButton *m_infoButton;
    QPushButton *m_appsButton;
    QPushButton *m_galleryButton;
    QPushButton *m_filesButton;
    QButtonGroup *m_navigationGroup;
};

#ifndef DEVICEPENDINGSIDEBARITEM_H
#define DEVICEPENDINGSIDEBARITEM_H
class DevicePendingSidebarItem : public QFrame
{
    Q_OBJECT
public:
    explicit DevicePendingSidebarItem(const QString &udid,
                                      QWidget *parent = nullptr);
    void setSelected(bool selected);
    bool isSelected() const { return m_selected; }

signals:
    void clicked();

protected:
    void mousePressEvent(QMouseEvent *event) override;

private:
    QString m_udid;
    bool m_selected = false;
};
#endif // DEVICEPENDINGSIDEBARITEM_H

#ifndef RECOVERYDEVICESIDEBARITEM_H
#define RECOVERYDEVICESIDEBARITEM_H
class RecoveryDeviceSidebarItem : public QFrame
{
    Q_OBJECT
public:
    explicit RecoveryDeviceSidebarItem(uint64_t ecid,
                                       QWidget *parent = nullptr);

    void setSelected(bool selected);
    bool isSelected() const { return m_selected; }

private:
    void setupUI();
    uint64_t m_ecid;
    bool m_selected = false;
signals:
    void recoveryDeviceSelected(uint64_t ecid);
};
#endif // RECOVERYDEVICESIDEBARITEM_H

// Unified device selection data
struct DeviceSelection {
    enum Type { Normal, Recovery, Pending };
    Type type;
    std::string udid;
    uint64_t ecid = 0;
    QString section = "Info";

    bool valid() const
    {
        if (type == Normal) {
            return !udid.empty();
        } else if (type == Recovery) {
            return ecid != 0;
        } else if (type == Pending) {
            return !udid.empty();
        }
        return false;
    }

    DeviceSelection(const std::string &deviceUdid, const QString &nav = "")
        : type(Normal), udid(deviceUdid), section(nav)
    {
    }
    DeviceSelection(uint64_t recoveryEcid) : type(Recovery), ecid(recoveryEcid)
    {
    }
    static DeviceSelection pending(const std::string &deviceUuid)
    {
        DeviceSelection sel(deviceUuid);
        sel.type = Pending;
        return sel;
    }
};

class DeviceSidebarWidget : public QWidget
{
    Q_OBJECT

public:
    explicit DeviceSidebarWidget(QWidget *parent = nullptr);

    // Unified interface
    DeviceSidebarItem *addDevice(const QString &deviceName,
                                 const std::string &uuid, bool isWireless);
    DevicePendingSidebarItem *addPendingDevice(const QString &uuid);
    RecoveryDeviceSidebarItem *addRecoveryDevice(uint64_t ecid);

    void removeDevice(const std::string &uuid);
    void removePendingDevice(const std::string &uuid);
    void removeRecoveryDevice(uint64_t ecid);

    void setCurrentSelection(const DeviceSelection &selection);

public slots:
    void onItemSelected(const DeviceSelection &selection);

signals:
    void deviceSelectionChanged(const DeviceSelection &selection);

private:
    void updateSelection();
    QScrollArea *m_scrollArea;
    QWidget *m_contentWidget;
    QVBoxLayout *m_contentLayout;

    DeviceSelection m_currentSelection;
    QMap<std::string, DeviceSidebarItem *> m_deviceItems;
    QMap<std::string, DevicePendingSidebarItem *> m_pendingItems;
    QMap<uint64_t, RecoveryDeviceSidebarItem *> m_recoveryItems;
};

#endif // DEVICESIDEBARWIDGET_H