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

#ifndef TOOLBOXWIDGET_H
#define TOOLBOXWIDGET_H

#include "airplaywidget.h"
#include "devdiskimageswidget.h"
#include "devicesidebarwidget.h"
#include "iDescriptor-ui.h"
#include "iDescriptor.h"
#include "networkdeviceswidget.h"
#include "wirelessgalleryimportwidget.h"
#include <QComboBox>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QScrollArea>
#include <QVBoxLayout>
#include <QWidget>
#ifndef __APPLE__
#include "ifusewidget.h"
#endif

class ToolboxItemWidget : public ClickableWidget
{
    Q_OBJECT
public:
    ToolboxItemWidget(iDescriptorTool tool, const QString &description,
                      const QString &iconName, const QString &title,
                      bool requiresDevice, bool iconThemable,
                      QWidget *parent = nullptr)
        : ClickableWidget(parent)
    {
        setCursor(Qt::PointingHandCursor);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
        setStyleSheet("padding: 5px; border: none; outline: none;");
        QVBoxLayout *layout = new QVBoxLayout(this);
        ZIconLabel *icon = new ZIconLabel(QIcon(iconName), nullptr, 1.5, this);
        if (!iconThemable) {
            icon->setIconThemable(false);
        }
        QLabel *titleLabel = new QLabel(title);
        titleLabel->setAlignment(Qt::AlignCenter);

        // Description
        QLabel *descLabel = new QLabel(description);
        descLabel->setWordWrap(true);
        descLabel->setAlignment(Qt::AlignCenter);
        descLabel->setStyleSheet("color: #666; font-size: 12px;");
        icon->setIconSizeMultiplier(1.90);

        layout->addWidget(icon, 0, Qt::AlignCenter);
        layout->addWidget(titleLabel);
        layout->addWidget(descLabel);
    }

    void updateStyles(bool enabled)
    {
        // FIXME: Opacity does not work because of the stylesheet on Windows
#ifndef WIN32
        if (enabled) {
            setStyleSheet("QWidget#toolboxFrame { "
                          "padding: 5px; }");
            setEnabled(true);
        } else {
            setStyleSheet("QWidget#toolboxFrame { "
                          "padding: 5px;"
                          "opacity: 0.45;  }");
            setEnabled(false);
        }
#else
        if (enabled) {
            setStyleSheet("QWidget#toolboxFrame { padding: 5px; "
                          "border: none; outline: none; }");
            setCursor(Qt::PointingHandCursor);
            setEnabled(true);
        } else {
            setStyleSheet("padding: 5px;"
                          "border-radius: 8px;"
                          "color: #666;");
            setCursor(Qt::ArrowCursor);
            setEnabled(false);
        }
#endif
    }
};

class ToolboxWidget : public QWidget
{
    Q_OBJECT
public:
    explicit ToolboxWidget(QWidget *parent = nullptr);
    static void restartDevice(const std::shared_ptr<iDescriptorDevice> device);
    static void shutdownDevice(const std::shared_ptr<iDescriptorDevice> device);
    static void
    enterRecoveryMode(const std::shared_ptr<iDescriptorDevice> device);
    static ToolboxWidget *sharedInstance();
    void restartAirPlayWidget();
private slots:
    void onDeviceSelectionChanged();
    void onToolboxClicked(iDescriptorTool tool, bool requiresDevice);
    void onCurrentDeviceChanged(const DeviceSelection &selection);

private:
    void setupUI();
    void updateDeviceList();
    void updateToolboxStates();
    void updateUI();
    ToolboxItemWidget *createToolbox(iDescriptorTool tool,
                                     const QString &description,
                                     bool requiresDevice);
    QComboBox *m_deviceCombo;
    QLabel *m_deviceLabel;
    QScrollArea *m_scrollArea;
    QWidget *m_contentWidget;
    QGridLayout *m_gridLayout;
    QList<ToolboxItemWidget *> m_toolboxes;
    QString m_uuid;
    DevDiskImagesWidget *m_devDiskImagesWidget = nullptr;
    NetworkDevicesWidget *m_networkDevicesWidget = nullptr;
    AirPlayWidget *m_airplayWidget = nullptr;
#ifndef __APPLE__
    iFuseWidget *m_ifuseWidget = nullptr;
#endif
    WirelessGalleryImportWidget *m_wirelessGalleryImportWidget = nullptr;

signals:
};

#endif // TOOLBOXWIDGET_H
