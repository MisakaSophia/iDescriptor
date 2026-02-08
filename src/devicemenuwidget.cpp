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

#include "devicemenuwidget.h"
// #include "Toast.h"
#include "cableinfowidget.h"
#include "devdiskimageswidget.h"
#include "iDescriptor.h"
#include "livescreenwidget.h"
#include "qprocessindicator.h"
#include "querymobilegestaltwidget.h"
#include "servicemanager.h"
#include "virtuallocationwidget.h"
#include <QDebug>
#include <QStackedWidget>
#include <QVBoxLayout>

DeviceMenuWidget::DeviceMenuWidget(iDescriptorDevice *device, QWidget *parent)
    : QWidget{parent}, m_device(device)
{
    QVBoxLayout *mainLayout = new QVBoxLayout(this);
    setContentsMargins(0, 0, 0, 0);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(0);

    stackedWidget = new QStackedWidget(this);
    mainLayout->addWidget(stackedWidget);

    QProcessIndicator *loadingIndicator = new QProcessIndicator();
    loadingIndicator->setType(QProcessIndicator::line_rotate);
    loadingIndicator->setFixedSize(64, 32);

    QWidget *loadingWidget = new QWidget();
    QVBoxLayout *loadingLayout = new QVBoxLayout(loadingWidget);
    loadingLayout->setAlignment(Qt::AlignCenter);
    loadingLayout->addWidget(loadingIndicator, 0, Qt::AlignCenter);
    loadingIndicator->start();
    stackedWidget->addWidget(loadingWidget);
    stackedWidget->setCurrentIndex(0);

    QTimer::singleShot(100, this, &DeviceMenuWidget::init);
}

void DeviceMenuWidget::init()
{

    // Create and add widgets to the stacked widget
    m_deviceInfoWidget = new DeviceInfoWidget(m_device, this);
    m_installedAppsWidget = new InstalledAppsWidget(m_device, this);
    m_galleryWidget = new GalleryWidget(m_device, this);
    m_fileExplorerWidget = new FileExplorerWidget(m_device, this);

    // Set minimum heights
    m_galleryWidget->setMinimumHeight(300);
    m_fileExplorerWidget->setMinimumHeight(300);

    stackedWidget->addWidget(m_deviceInfoWidget);    // Index 0 - Info
    stackedWidget->addWidget(m_installedAppsWidget); // Index 1 - Apps
    stackedWidget->addWidget(m_galleryWidget);       // Index 2 - Gallery
    stackedWidget->addWidget(m_fileExplorerWidget);  // Index 3 - Files

    // Set default to Info tab
    stackedWidget->setCurrentWidget(m_deviceInfoWidget);

    // Connect to current changed signal for lazy loading
    connect(stackedWidget, &QStackedWidget::currentChanged, this,
            [this](int index) {
                if (stackedWidget->widget(index) ==
                    m_galleryWidget) { // Gallery tab
                    m_galleryWidget->load();
                }
            });

    QWidget *loadingWidget = stackedWidget->widget(0);
    stackedWidget->removeWidget(loadingWidget);
    loadingWidget->deleteLater();

    // FIXME: toast really necessary here?
    //  if (m_device->deviceInfo.parsedDeviceVersion.major < 13) {
    //      Toast *toast = new Toast(this);
    //      toast->setAttribute(Qt::WA_DeleteOnClose);
    //      toast->setDuration(8000); // Hide after 8 seconds
    //      toast->setTitle("Not wireless compatible");
    //      toast->setText("This device is not wireless compatible.");
    //      toast->setPosition(ToastPosition::BOTTOM_MIDDLE);
    //      toast->show();
    //  } else {
    //      if (m_device->deviceInfo.isWireless)
    //          return;
    //      bool enabled = ServiceManager::enableWirelessConnections(m_device);
    //      Toast *toast = new Toast(this);
    //      toast->setAttribute(Qt::WA_DeleteOnClose);
    //      toast->setDuration(8000); // Hide after 8 seconds
    //      toast->setPosition(ToastPosition::BOTTOM_MIDDLE);
    //      if (enabled) {
    //          toast->setTitle("Wireless connections enabled");
    //          toast->setText(
    //              "You can now use wireless connections with this device.");
    //      } else {
    //          toast->setTitle("Failed to enable wireless connections");
    //          toast->setText(
    //              "Could not enable wireless connections for this device.");
    //      }
    //      toast->show();
    //  }
}

void DeviceMenuWidget::switchToTab(const QString &tabName)
{
    if (tabName == "Info") {
        stackedWidget->setCurrentWidget(m_deviceInfoWidget);
    } else if (tabName == "Apps") {
        stackedWidget->setCurrentWidget(m_installedAppsWidget);
    } else if (tabName == "Gallery") {
        qDebug() << "Switching to Gallery tab";
        stackedWidget->setCurrentWidget(m_galleryWidget);
    } else if (tabName == "Files") {
        stackedWidget->setCurrentWidget(m_fileExplorerWidget);
    } else {
        qDebug() << "Tab not found:" << tabName;
    }
}

DeviceMenuWidget::~DeviceMenuWidget()
{
    qDebug() << "DeviceMenuWidget destructor called";
}
