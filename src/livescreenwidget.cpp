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

#include "livescreenwidget.h"
#include "appcontext.h"
#include "devdiskimagehelper.h"
#include "devdiskmanager.h"
#include "iDescriptor.h"
#include <QDebug>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QSpinBox>
#include <QTimer>
#include <QTransform>
#include <QVBoxLayout>

LiveScreenWidget::LiveScreenWidget(iDescriptorDevice *device, QWidget *parent)
    : Tool{parent, false}, m_device(device)
{
    setWindowTitle("Live Screen - iDescriptor");
    setAttribute(Qt::WA_DeleteOnClose);

    unsigned int deviceMajorVersion =
        m_device->deviceInfo.parsedDeviceVersion.major;

    if (deviceMajorVersion > 16) {
        QMessageBox::warning(
            this, "Unsupported iOS Version",
            "Real-time Screen feature requires iOS 16 or earlier.\n"
            "Your device is running iOS " +
                QString::number(deviceMajorVersion) +
                ", which is not yet supported.");
        QTimer::singleShot(0, this, &QWidget::close);
        return;
    }

    connect(AppContext::sharedInstance(), &AppContext::deviceRemoved, this,
            [this, device](const std::string &removed_uuid) {
                if (device->udid == removed_uuid) {
                    this->close();
                    this->deleteLater();
                }
            });

    // Setup UI
    QVBoxLayout *mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(10, 10, 10, 10);
    mainLayout->setSpacing(10);

    // Status label
    m_statusLabel = new QLabel("Initializing...");
    m_statusLabel->setAlignment(Qt::AlignCenter);
    mainLayout->addWidget(m_statusLabel);

    // Screenshot display
    m_imageLabel = new QLabel();
    m_imageLabel->setMinimumSize(300, 600);
    m_imageLabel->setAlignment(Qt::AlignCenter);
    mainLayout->addWidget(m_imageLabel, 1);

    // Controls (rotate / mirror), initially hidden, shown when capturing starts
    m_controlsWidget = new QWidget(this);
    auto *controlsLayout = new QHBoxLayout(m_controlsWidget);
    controlsLayout->setContentsMargins(0, 0, 0, 0);
    controlsLayout->setSpacing(5);

    m_rotateCwButton = new QPushButton("Rotate ↻", m_controlsWidget);
    m_rotateCcwButton = new QPushButton("Rotate ↺", m_controlsWidget);
    m_mirrorButton = new QPushButton("Mirror", m_controlsWidget);

    controlsLayout->addWidget(m_rotateCwButton);
    controlsLayout->addWidget(m_rotateCcwButton);
    controlsLayout->addWidget(m_mirrorButton);
    controlsLayout->addStretch(1);

    m_controlsWidget->setVisible(false);
    mainLayout->addWidget(m_controlsWidget);

    // button actions
    connect(m_rotateCwButton, &QPushButton::clicked, this, [this]() {
        m_rotationDegrees = (m_rotationDegrees + 90) % 360;
        applyTransformAndDisplay();
    });
    connect(m_rotateCcwButton, &QPushButton::clicked, this, [this]() {
        m_rotationDegrees = (m_rotationDegrees + 270) % 360; // -90 mod 360
        applyTransformAndDisplay();
    });
    connect(m_mirrorButton, &QPushButton::clicked, this, [this]() {
        m_mirrorHorizontal = !m_mirrorHorizontal;
        applyTransformAndDisplay();
    });

    QTimer::singleShot(0, this, &LiveScreenWidget::startInitialization);
}

void LiveScreenWidget::startInitialization()
{
    const bool initializeScreenshotServiceSuccess =
        initializeScreenshotService(false);
    if (initializeScreenshotServiceSuccess)
        return;

    // Start the initialization process - auto-mount mode
    auto *helper = new DevDiskImageHelper(m_device, this);

    connect(helper, &DevDiskImageHelper::mountingCompleted, this,
            [this, helper](bool success) {
                helper->deleteLater();

                if (success) {
                    // for some reason it does not work immediately, so delay a
                    // bit
                    QTimer::singleShot(1000, this, [this]() {
                        initializeScreenshotService(true);
                    });
                } else {
                    m_statusLabel->setText(
                        "Failed to mount developer disk image");
                }
            });

    helper->start();
}

LiveScreenWidget::~LiveScreenWidget()
{
    if (m_thread) {
        m_thread->requestInterruption();
        m_thread->wait();
        m_thread->deleteLater();
        m_thread = nullptr;
    }
    if (m_screenshotrClient) {
        screenshotr_client_free(m_screenshotrClient);
        m_screenshotrClient = nullptr;
    }
}

bool LiveScreenWidget::initializeScreenshotService(bool notify)
{
    try {
        m_statusLabel->setText("Connecting to screenshot service...");
        IdeviceFfiError *err =
            screenshotr_connect(m_device->provider, &m_screenshotrClient);
        if (err) {
            qDebug() << "Failed to create Screenshotr client";
            return false; // proceed to mount image
        }
        // Successfully initialized, start capturing
        m_statusLabel->setText("Capturing");
        if (m_controlsWidget)
            m_controlsWidget->setVisible(true); // show rotation/mirror buttons
        startCapturing();
        return true;
    } catch (const std::exception &e) {
        m_statusLabel->setText("Exception occurred");
        if (notify)
            QMessageBox::critical(
                this, "Exception",
                QString("Exception occurred: %1").arg(e.what()));
        return false;
    }
}

void LiveScreenWidget::startCapturing()
{
    if (!m_screenshotrClient) {
        qWarning()
            << "Cannot start capturing: screenshot client not initialized";
        return;
    }

    m_thread = new ScreenshotrThread(m_screenshotrClient, m_device, this);
    connect(m_thread, &ScreenshotrThread::screenshotCaptured, this,
            [this](const QPixmap &pixmap) {
                // store raw pixmap and render with current rotation/mirror
                m_lastPixmap = pixmap;
                applyTransformAndDisplay();
            });
    m_thread->start();
}

void LiveScreenWidget::applyTransformAndDisplay()
{
    if (m_lastPixmap.isNull() || !m_imageLabel)
        return;

    QTransform transform;
    transform.rotate(m_rotationDegrees);

    QPixmap transformed =
        m_lastPixmap.transformed(transform, Qt::SmoothTransformation);

    if (m_mirrorHorizontal) {
        QTransform mirrorTransform;
        mirrorTransform.scale(-1, 1);
        transformed =
            transformed.transformed(mirrorTransform, Qt::SmoothTransformation);
    }

    const QSize targetSize = m_imageLabel->size();
    if (!targetSize.isEmpty()) {
        transformed = transformed.scaled(targetSize, Qt::KeepAspectRatio,
                                         Qt::SmoothTransformation);
    }

    m_imageLabel->setPixmap(transformed);
}