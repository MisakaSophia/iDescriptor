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
#include "settingsmanager.h"
#include "zloadingwidget.h"
#include <QDebug>
#include <QHBoxLayout>
#include <QMessageBox>
#include <QTimer>
#include <QVBoxLayout>

DevDiskImageHelper::DevDiskImageHelper(
    const std::shared_ptr<iDescriptorDevice> device, QWidget *parent)
    : QDialog(parent), m_device(device)
{
    setAttribute(Qt::WA_DeleteOnClose);
#ifdef WIN32
    setupWinWindow(this);
#endif
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

    m_loadingWidget = new ZLoadingWidget(true, this);
    connect(m_loadingWidget, &ZLoadingWidget::retryClicked, this,
            &DevDiskImageHelper::onRetryButtonClicked);

    auto *contentLayout = new QHBoxLayout();
    contentLayout->addStretch();

    m_statusLabel = new QLabel("Please wait...");
    m_statusLabel->setWordWrap(true);
    m_statusLabel->setAlignment(Qt::AlignCenter);
    contentLayout->addWidget(m_statusLabel);
    contentLayout->addStretch();

    m_loadingWidget->setupContentWidget(contentLayout);
    mainLayout->addWidget(m_loadingWidget);

    setMinimumWidth(400);
    setMinimumHeight(200);
    setModal(true);
    show();
}

/* try to mount a specific version */
void DevDiskImageHelper::mountVersion(const QString &version)
{
    m_loadingWidget->stop();
    m_statusLabel->setText("Please wait...");
    m_version = version;
    unsigned int deviceMajorVersion =
        m_device->deviceInfo.parsedDeviceVersion.major;

    connect(
        m_device->service_manager,
        &CXX::ServiceManager::mounted_image_retrieved, this,
        [this, deviceMajorVersion, version](bool success, bool locked,
                                            QByteArray signature,
                                            u_int64_t sig_length) {
            if (!success) {
                if (locked) {
                    qDebug() << "Failed to retrieve mounted image signature: "
                                "device is locked.";
                    m_loadingWidget->showError(
                        "The device appears to be locked. Please unlock the "
                        "device and try again.");
                    return;
                }
                qDebug() << "Failed to retrieve mounted image signature.";
                m_loadingWidget->showError(
                    "Failed to retrieve mounted image signature.");
                return;
            }

            if (!signature.isEmpty() || sig_length > 0) {
                m_loadingWidget->showError(
                    "A developer disk image already mounted. "
                    "Please restart the device and try again.");
            } else {
                const QString downloadPath =
                    SettingsManager::sharedInstance()->devdiskimgpath();
                const bool isDownloaded =
                    DevDiskManager::sharedInstance()->isImageDownloaded(
                        version, downloadPath);

                qDebug() << "isDownloaded:" << isDownloaded;
                if (!isDownloaded) {
                    m_loadingWidget->showError(
                        "The developer disk image for iOS " + version +
                        " is not downloaded. Please download it first.");
                } else {
                    handleMounting(version);
                }
            }
        },
        Qt::SingleShotConnection);
    m_device->service_manager->get_mounted_image();
}

/* mount a compatible version */
void DevDiskImageHelper::start()
{
    m_loadingWidget->stop();
    m_statusLabel->setText("Please wait...");

    unsigned int deviceMajorVersion =
        m_device->deviceInfo.parsedDeviceVersion.major;

    // FIXME:we dont have developer disk images for ios 6 and below
    if (deviceMajorVersion > 5) {
        checkAndMount();
    } else {
        m_loadingWidget->showError(
            "Developer disk image is not available for iOS version " +
            QString::number(deviceMajorVersion) +
            ". Please use a device with iOS 6 or above.");
        return;
    }
}

void DevDiskImageHelper::checkAndMount()
{
    unsigned int deviceMajorVersion =
        m_device->deviceInfo.parsedDeviceVersion.major;

    connect(
        m_device->service_manager,
        &CXX::ServiceManager::mounted_image_retrieved, this,
        [this, deviceMajorVersion](bool success, bool locked,
                                   QByteArray signature, u_int64_t sig_length) {
            qDebug() << "[ DevDiskImageHelper::checkAndMount] qobject::connect "
                        "of mounted_image_retrieved consumed";
            if (!success) {
                if (locked) {
                    qDebug() << "Failed to retrieve mounted image signature: "
                                "device is locked.";
                    m_loadingWidget->showError(
                        "The device appears to be locked. Please unlock the "
                        "device and try again.");
                    return;
                }
                qDebug() << "Failed to retrieve mounted image info.";
                m_loadingWidget->showError(
                    "Failed to retrieve mounted image info.");
                return;
            }

            if (!signature.isEmpty() || sig_length > 0) {
                qDebug()
                    << "Developer disk image already mounted with signature:"
                    << "length:" << sig_length << "signature:" << signature;
                finishWithSuccess();
            } else {
                const QString version =
                    DevDiskManager::sharedInstance()->downloadCompatibleImage(
                        m_device, [this](bool success, const QString &version) {
                            if (success) {
                                handleMounting(version);
                            } else {
                                m_loadingWidget->showError(
                                    "Failed to download compatible image.");
                            }
                        });
                m_version = version;
                qDebug() << "Is there a compatible image ?"
                         << !version.isEmpty();
                if (version.isEmpty()) {
                    // FIXME: we need to disable the retry button in this case
                    m_loadingWidget->showError(
                        "There is no compatible developer disk "
                        "image available for " +
                        QString::number(deviceMajorVersion) + ".");
                } else {
                    m_statusLabel->setText(
                        QString("Downloading compatible developer disk "
                                "image for iOS %1..")
                            .arg(deviceMajorVersion));
                }
            }
        },
        Qt::SingleShotConnection);
    m_device->service_manager->get_mounted_image();
}
// todo called twice
//  finishWithSuccess called with wait = false
void DevDiskImageHelper::handleMounting(const QString &version)
{
    m_statusLabel->setText("Mounting...");
    auto paths = DevDiskManager::sharedInstance()->getPathsForVersion(version);
    qDebug() << "Mounting image with paths:" << paths.first << paths.second;

    connect(
        m_device->service_manager, &CXX::ServiceManager::dev_image_mounted,
        this,
        [this](bool success, bool isLocked) {
            qDebug() << "[devdiskimagehelper] : Developer disk image "
                        "mount result:"
                     << success;
            if (success) {
                qDebug() << "[devdiskimagehelper] : Developer disk image "
                            "mounted successfully.";
                finishWithSuccess(true);
            } else {
                if (isLocked) {
                    qDebug() << "[devdiskimagehelper] : Failed to mount "
                                "developer disk image: device is locked.";
                    m_loadingWidget->showError(
                        "Failed to mount developer disk image.\n"
                        "The device appears to be locked. Please unlock the "
                        "device and try again.");
                    return;
                } else {

                    qDebug()
                        << "[devdiskimagehelper] : Failed to mount developer "
                           "disk image.";
                    m_loadingWidget->showError(
                        "Failed to mount developer disk image.\n"
                        "Please ensure the device is unlocked and "
                        "using a genuine "
                        "cable.");
                }
            }
        },
        Qt::SingleShotConnection);

    m_device->service_manager->mount_dev_image(paths.first, paths.second);
}

void DevDiskImageHelper::onRetryButtonClicked()
{
    m_loadingWidget->showLoading();

    QTimer::singleShot(200, this, [this]() {
        if (!m_version.isEmpty()) {
            qDebug() << "Retrying mount for version:" << m_version;
            mountVersion(m_version);
        } else {
            start();
        }
    });
}

/*
    waiting is sometimes required because services
    may not become available
    as soon as the img is mounted
*/
void DevDiskImageHelper::finishWithSuccess(bool wait)
{
    qDebug() << "finishWithSuccess called with wait =" << wait;
    auto handler = [this]() {
        if (m_loadingWidget) {
            m_loadingWidget->stop(false);
        }
        accept();
    };
    if (wait) {
        return QTimer::singleShot(3000, handler);
    }
    handler();
}
