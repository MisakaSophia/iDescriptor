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

#include "devdiskimagehelper.h"
#include "appcontext.h"
#include "devdiskmanager.h"
#include "qprocessindicator.h"
#include "servicemanager.h"
#include "settingsmanager.h"
#include <QDebug>
#include <QHBoxLayout>
#include <QMessageBox>
#include <QTimer>
#include <QVBoxLayout>

DevDiskImageHelper::DevDiskImageHelper(iDescriptorDevice *device,
                                       QWidget *parent)
    : QDialog(parent), m_device(device)
{
    setAttribute(Qt::WA_DeleteOnClose);
    setWindowTitle("Developer Disk Image - iDescriptor");
    setupUI();

    connect(this, &QDialog::accepted, this,
            [this]() { emit mountingCompleted(true); });
    connect(this, &QDialog::rejected, this,
            [this]() { emit mountingCompleted(false); });
}

void DevDiskImageHelper::setupUI()
{
    auto *mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(20, 20, 20, 20);
    mainLayout->setSpacing(15);

    // Loading indicator
    auto *indicatorLayout = new QHBoxLayout();
    indicatorLayout->addStretch();
    m_loadingIndicator = new QProcessIndicator();
    m_loadingIndicator->setType(QProcessIndicator::line_rotate);
    m_loadingIndicator->setFixedSize(64, 32);
    indicatorLayout->addWidget(m_loadingIndicator);
    indicatorLayout->addStretch();
    mainLayout->addLayout(indicatorLayout);

    // Status label
    m_statusLabel = new QLabel("Checking developer disk image...");
    m_statusLabel->setWordWrap(true);
    m_statusLabel->setAlignment(Qt::AlignCenter);
    mainLayout->addWidget(m_statusLabel);

    // Button layout
    auto *buttonLayout = new QHBoxLayout();
    buttonLayout->addStretch();

    m_mountButton = new QPushButton("Mount");
    m_mountButton->setDefault(true);
    m_mountButton->setVisible(false);
    connect(m_mountButton, &QPushButton::clicked, this,
            &DevDiskImageHelper::onMountButtonClicked);
    buttonLayout->addWidget(m_mountButton);

    m_retryButton = new QPushButton("Retry");
    m_retryButton->setVisible(false);
    connect(m_retryButton, &QPushButton::clicked, this,
            &DevDiskImageHelper::onRetryButtonClicked);
    buttonLayout->addWidget(m_retryButton);

    m_cancelButton = new QPushButton("Cancel");
    connect(m_cancelButton, &QPushButton::clicked, this, &QDialog::reject);
    buttonLayout->addWidget(m_cancelButton);

    buttonLayout->addStretch();
    mainLayout->addLayout(buttonLayout);

    setMinimumWidth(400);
    setModal(true);
}

void DevDiskImageHelper::start()
{
    m_loadingIndicator->start();
    showStatus("Please wait...");

    unsigned int deviceMajorVersion =
        m_device->deviceInfo.parsedDeviceVersion.major;

    // FIXME:we dont have developer disk images for ios 6 and below
    if (deviceMajorVersion > 5) {
        const bool isMountAvailable =
            DevDiskManager::sharedInstance()->downloadCompatibleImage(
                m_device, [this](bool success) {
                    if (success) {
                        checkAndMount();
                    } else {
                        finishWithError("Failed to download compatible image.");
                    }
                });
        qDebug() << "isMountAvailable:" << isMountAvailable;
        if (!isMountAvailable) {
            finishWithError(
                "There is no compatible developer disk image available for " +
                QString::number(deviceMajorVersion) + ".");
        }
    } else {
        finishWithSuccess();
        return;
    }
}

void DevDiskImageHelper::checkAndMount()
{
    MountedImageInfo info = ServiceManager::getMountedImage(
        AppContext::sharedInstance()->getDevice(m_device->udid));
    if (info.err && info.err->code != NotFoundErrorCode) {
        onMountButtonClicked();
        return;
    }

    // If image is already mounted
    if (info.signature && info.signature_len) {
        finishWithSuccess();
        return;
    }

    onMountButtonClicked();
}

void DevDiskImageHelper::onMountButtonClicked()
{
    QString path = SettingsManager::sharedInstance()->mkDevDiskImgPath();
    m_mountButton->setVisible(false);
    m_loadingIndicator->start();

    // Check if we need to download first
    unsigned int deviceMajorVersion =
        m_device->deviceInfo.parsedDeviceVersion.major;
    unsigned int deviceMinorVersion =
        m_device->deviceInfo.parsedDeviceVersion.minor;

    QList<ImageInfo> images = DevDiskManager::sharedInstance()->parseImageList(
        path, deviceMajorVersion, deviceMinorVersion, "", 0);

    // Check if compatible image is downloaded
    bool hasDownloadedImage = false;
    QString versionToMount;

    for (const ImageInfo &info : images) {
        if (info.compatibility == ImageCompatibility::Compatible ||
            info.compatibility == ImageCompatibility::MaybeCompatible) {
            if (info.isDownloaded) {
                hasDownloadedImage = true;
                versionToMount = info.version;
                break;
            }
        }
    }

    if (hasDownloadedImage) {
        // // Mount directly
        m_downloadingVersion = versionToMount;
        showStatus("Mounting developer disk image...");
        onImageDownloadFinished(versionToMount, true, "");
    } else {
        // Need to download first
        showStatus(
            "Downloading developer disk image...\nThis may take a moment.");

        // Connect to download signals
        connect(DevDiskManager::sharedInstance(),
                &DevDiskManager::imageDownloadFinished, this,
                &DevDiskImageHelper::onImageDownloadFinished,
                Qt::UniqueConnection);

        // Find version to download
        for (const ImageInfo &info : images) {
            if (info.compatibility == ImageCompatibility::Compatible ||
                info.compatibility == ImageCompatibility::MaybeCompatible) {
                m_downloadingVersion = info.version;
                break;
            }
        }
    }
}

void DevDiskImageHelper::onImageDownloadFinished(const QString &version,
                                                 bool success,
                                                 const QString &errorMessage)
{
    if (version != m_downloadingVersion) {
        qDebug() << "Ignoring download finished for version" << version
                 << "expected" << m_downloadingVersion;
        return;
    }

    if (!success) {
        showRetryUI("Failed to download developer disk image:\n" +
                    errorMessage);
        return;
    }

    showStatus("Download complete. Mounting...");

    auto paths = DevDiskManager::sharedInstance()->getPathsForVersion(version);

    IdeviceFfiError *err =
        ServiceManager::mountImage(m_device, paths.first.toStdString().c_str(),
                                   paths.second.toStdString().c_str());

    if (err == nullptr) {
        return finishWithSuccess();
    }

    qDebug() << "onImageDownloadFinished:" << err->code
             << QString::fromStdString(err->message);

    if (err->code == DeviceLockedMountErrorCode) {
        showRetryUI(
            "Device is locked. Please unlock your device and try again.");

    } else {
        showRetryUI(
            "Failed to mount developer disk image.\n"
            "Please ensure the device is unlocked and using a genuine cable.");
    }
    idevice_error_free(err);
}

void DevDiskImageHelper::showRetryUI(const QString &errorMessage)
{
    m_loadingIndicator->stop();
    showStatus(errorMessage, true);
    m_mountButton->setVisible(false);
    m_retryButton->setVisible(true);
    m_cancelButton->setText("Close");
}

void DevDiskImageHelper::onRetryButtonClicked()
{
    m_retryButton->setVisible(false);
    m_cancelButton->setText("Cancel");
    start();
}

void DevDiskImageHelper::showStatus(const QString &message, bool isError)
{
    m_statusLabel->setText(message);

    show();
}

void DevDiskImageHelper::finishWithSuccess()
{
    m_loadingIndicator->stop();
    accept();
}

void DevDiskImageHelper::finishWithError(const QString &errorMessage)
{
    m_loadingIndicator->stop();
    showStatus(errorMessage, true);
}
