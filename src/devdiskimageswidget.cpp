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

#include "devdiskimageswidget.h"
#include "appcontext.h"
#include "devdiskmanager.h"
#include "iDescriptor.h"
#include "qprocessindicator.h"
#include "settingsmanager.h"
#include <QCloseEvent>
#include <QComboBox>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QGraphicsDropShadowEffect>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QProgressBar>
#include <QPushButton>
#include <QStackedWidget>
#include <QStandardPaths>
#include <QStringList>
#include <QVBoxLayout>
#include <string>

DevDiskImagesWidget::DevDiskImagesWidget(const QString &deviceUdid,
                                         QWidget *parent)
    : Tool(parent), m_currentDeviceUdid(deviceUdid)
{
    setMinimumSize(400, 400);
    resize(800, 600);
    setupUi();
    connect(DevDiskManager::sharedInstance(), &DevDiskManager::imageListFetched,
            this, &DevDiskImagesWidget::onImageListFetched);

    updateDeviceList();
    connect(AppContext::sharedInstance(), &AppContext::deviceAdded, this,
            &DevDiskImagesWidget::updateDeviceList);
    connect(AppContext::sharedInstance(), &AppContext::deviceRemoved, this,
            &DevDiskImagesWidget::updateDeviceList);
    connect(m_deviceComboBox,
            QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            &DevDiskImagesWidget::onDeviceSelectionChanged);

    connect(m_imageListWidget, &QListWidget::itemClicked, this,
            [this](QListWidgetItem *item) {
                m_mountButton->setEnabled(item != nullptr);
            });
}

void DevDiskImagesWidget::setupUi()
{
    setWindowTitle("Developer Disk Images - iDescriptor");
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);

    auto *mountLayout = new QHBoxLayout();
    mountLayout->addWidget(new QLabel("Device:"));
    m_deviceComboBox = new QComboBox(this);
    mountLayout->addWidget(m_deviceComboBox);
    m_mountButton = new QPushButton("Mount", this);
    m_mountButton->setEnabled(false);
    m_check_mountedButton = new QPushButton("Check Mounted", this);
    connect(m_mountButton, &QPushButton::clicked, this,
            &DevDiskImagesWidget::onMountButtonClicked);
    connect(m_check_mountedButton, &QPushButton::clicked, this,
            &DevDiskImagesWidget::checkMountedImage);
    mountLayout->setContentsMargins(10, 10, 10, 10);
    mountLayout->addWidget(m_mountButton);
    mountLayout->addWidget(m_check_mountedButton);
    layout->addLayout(mountLayout);

    m_stackedWidget = new QStackedWidget(this);
    layout->addWidget(m_stackedWidget);

    // Create loading page with process indicator
    auto *loadingPage = new QWidget();
    auto *loadingLayout = new QVBoxLayout(loadingPage);
    loadingLayout->addStretch();

    auto *indicatorLayout = new QHBoxLayout();
    indicatorLayout->addStretch();
    m_processIndicator = new QProcessIndicator(loadingPage);
    m_processIndicator->setFixedSize(40, 40);
    m_processIndicator->setType(QProcessIndicator::line_rotate);
    indicatorLayout->addWidget(m_processIndicator);
    indicatorLayout->addStretch();
    loadingLayout->addLayout(indicatorLayout);

    m_statusLabel = new QLabel("Fetching image list...");
    m_statusLabel->setAlignment(Qt::AlignCenter);
    m_statusLabel->setStyleSheet("QLabel { color: #666; margin-top: 10px; }");
    loadingLayout->addWidget(m_statusLabel);
    loadingLayout->addStretch();

    m_stackedWidget->addWidget(loadingPage);

    m_imageListWidget = new QListWidget(this);
    m_imageListWidget->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_imageListWidget->setStyleSheet(
        "QListWidget { background: transparent; border: none; }");

    m_stackedWidget->addWidget(m_imageListWidget);

    m_processIndicator->start();
    m_stackedWidget->setCurrentIndex(0); // Show loading page
    // TODO: we may force to refetch most up to date image list
    QTimer::singleShot(500, this, [this]() {
        displayImages();
        m_stackedWidget->setCurrentWidget(m_imageListWidget);
    });
}

void DevDiskImagesWidget::fetchImages()
{
    m_processIndicator->start();
    m_stackedWidget->setCurrentIndex(0); // Show loading page
    m_statusLabel->setText("Fetching image list...");
    // DevDiskManager::sharedInstance()->fetchImageList();
}

void DevDiskImagesWidget::onImageListFetched(bool success,
                                             const QString &errorMessage)
{
    m_processIndicator->stop();

    if (!success) {
        qDebug() << "Error fetching image list:" << errorMessage;
        m_statusLabel->setText(
            QString("Error fetching image list: %1").arg(errorMessage));
        // Keep showing the loading page with error message
        return;
    }

    qDebug() << "Image list fetched successfully";
    displayImages();
    m_stackedWidget->setCurrentWidget(m_imageListWidget);
}

void DevDiskImagesWidget::onDeviceSelectionChanged(int index)
{
    if (index < 0 ||
        index >= AppContext::sharedInstance()->getAllDevices().size())
        return;

    auto device = AppContext::sharedInstance()->getAllDevices()[index];
    if (device == nullptr)
        return;

    m_currentDeviceUdid = device->udid;
    displayImages();
}

void DevDiskImagesWidget::displayImages()
{
    qDebug() << "Displaying images for device";
    m_imageListWidget->clear();

    // Look up device by UDID
    std::shared_ptr<iDescriptorDevice> currentDevice = nullptr;
    if (!m_currentDeviceUdid.isEmpty()) {
        currentDevice =
            AppContext::sharedInstance()->getDevice(m_currentDeviceUdid);
    }
    bool hasConnectedDevice = (currentDevice != nullptr);

    int major = hasConnectedDevice
                    ? currentDevice->deviceInfo.parsedDeviceVersion.major
                    : 0;
    int minor = hasConnectedDevice
                    ? currentDevice->deviceInfo.parsedDeviceVersion.minor
                    : 0;

    QString path = SettingsManager::sharedInstance()->mkDevDiskImgPath();
    QList<ImageInfo> allImages =
        DevDiskManager::sharedInstance()->parseImageList(
            path, major, minor, m_mounted_sig.c_str(), m_mounted_sig_len);

    qDebug() << "Total images:" << allImages.size();

    int itemIndex = 0;

    // Create UI items
    auto createVersionItem = [&](const ImageInfo &info) {
        bool isCompatible =
            (info.compatibility == ImageCompatibility::Compatible ||
             info.compatibility == ImageCompatibility::MaybeCompatible);
        auto *itemWidget = new QWidget();
        itemWidget->setObjectName("itemWidget");
        auto *itemLayout = new QHBoxLayout(itemWidget);

        // TODO: maybe create a custom widget for this, if we ever need this
        // elsewhere ?
        QColor baseColor = QApplication::palette().color(QPalette::Window);
        QColor bgColor =
            itemIndex % 2 == 0 ? baseColor.lighter(110) : baseColor;
        itemWidget->setStyleSheet(
            QString("QWidget#itemWidget { background-color: %1; }")
                .arg(bgColor.name()));
        itemIndex++;

        auto *versionLabel = new QLabel(info.version);
        if (isCompatible) {
            if (info.compatibility == ImageCompatibility::Compatible) {
                versionLabel->setStyleSheet(
                    "QLabel { font-weight: bold; color: #2E7D32; }");
            } else if (info.compatibility ==
                       ImageCompatibility::MaybeCompatible) {
                versionLabel->setStyleSheet(
                    "QLabel { font-weight: bold; color: #F57C00; }");
            }
        }
        itemLayout->addWidget(versionLabel);

        // Add status labels
        if (hasConnectedDevice) {
            if (isCompatible) {
                if (info.isMounted) {
                    auto *mountedLabel = new QLabel("Mounted");
                    mountedLabel->setStyleSheet(
                        "QLabel { color: #1565C0; font-weight: bold; }");
                    itemLayout->addWidget(mountedLabel);
                } else if (info.compatibility ==
                           ImageCompatibility::MaybeCompatible) {
                    auto *maybeLabel = new QLabel("Maybe compatible");
                    maybeLabel->setStyleSheet("QLabel { color: #F57C00; "
                                              "margin-left: 10px; font-weight: "
                                              "bold; }");
                    itemLayout->addWidget(maybeLabel);
                }
            } else {
                auto *incompatLabel = new QLabel("Not compatible");
                incompatLabel->setStyleSheet(
                    "QLabel { color: #D32F2F; margin-left: 10px; font-weight: "
                    "bold; }");
                itemLayout->addWidget(incompatLabel);
            }
        }

        itemLayout->addStretch();

        auto *progressBar = new QProgressBar();
        progressBar->setVisible(false);
        itemLayout->addWidget(progressBar);

        auto *downloadButton =
            new QPushButton(info.isDownloaded ? "Re-download" : "Download");
        downloadButton->setDefault(true);
        downloadButton->setProperty("version", info.version);
        connect(downloadButton, &QPushButton::clicked, this,
                &DevDiskImagesWidget::onDownloadButtonClicked);
        itemLayout->addWidget(downloadButton);

        auto *listItem = new QListWidgetItem(m_imageListWidget);
        listItem->setSizeHint(itemWidget->sizeHint());
        m_imageListWidget->addItem(listItem);
        m_imageListWidget->setItemWidget(listItem, itemWidget);
    };

    bool hasCompatibleImages = false;
    bool hasOtherImages = false;
    bool separatorAdded = false;

    // Add all images, inserting separator when transitioning from compatible to
    // not compatible
    for (const auto &info : allImages) {
        bool isCompatible =
            (info.compatibility == ImageCompatibility::Compatible ||
             info.compatibility == ImageCompatibility::MaybeCompatible);

        if (isCompatible) {
            hasCompatibleImages = true;
        } else {
            hasOtherImages = true;
            // Add separator before first non-compatible image if we have
            // compatible ones
            if (hasCompatibleImages && !separatorAdded) {
                auto *separatorItem = new QListWidgetItem(m_imageListWidget);
                auto *separatorWidget = new QWidget();
                auto *separatorLayout = new QHBoxLayout(separatorWidget);
                auto *separatorLabel = new QLabel("Other versions");
                separatorLabel->setStyleSheet(
                    "QLabel { font-weight: bold; color: #757575; margin: 10px "
                    "0;}");
                separatorLayout->addWidget(separatorLabel);
                separatorItem->setSizeHint(separatorWidget->sizeHint());
                m_imageListWidget->addItem(separatorItem);
                m_imageListWidget->setItemWidget(separatorItem,
                                                 separatorWidget);
                separatorAdded = true;
            }
        }

        createVersionItem(info);
    }

    // Show device info if available
    if (hasConnectedDevice) {
        QString deviceVersion = QString("%1.%2").arg(major).arg(minor);
        m_statusLabel->setText(
            QString("Connected device: iOS %1 - Compatible images shown at top")
                .arg(deviceVersion));
    }
}

void DevDiskImagesWidget::onDownloadButtonClicked()
{
    auto *button = qobject_cast<QPushButton *>(sender());
    if (!button)
        return;

    QString version = button->property("version").toString();

    QString versionPath =
        QDir(SettingsManager::sharedInstance()->devdiskimgpath())
            .filePath(version);
    if (QDir(versionPath).exists()) {
        auto reply = QMessageBox::question(
            this, "Confirm Overwrite",
            QString(
                "Directory '%1' already exists. Do you want to overwrite it?")
                .arg(version),
            QMessageBox::Yes | QMessageBox::No);
        if (reply == QMessageBox::No) {
            return;
        }
    }

    startDownload(version);
}

void DevDiskImagesWidget::startDownload(const QString &version)
{
    // Find the button and progress bar for this version
    QPushButton *downloadButton = nullptr;
    QProgressBar *progressBar = nullptr;
    for (int i = 0; i < m_imageListWidget->count(); ++i) {
        auto *item = m_imageListWidget->item(i);
        auto *widget = m_imageListWidget->itemWidget(item);
        auto *button = widget->findChild<QPushButton *>();
        if (button && button->property("version") == version) {
            downloadButton = button;
            progressBar = widget->findChild<QProgressBar *>();
            break;
        }
    }

    if (!downloadButton || !progressBar)
        return;

    downloadButton->setEnabled(false);
    progressBar->setVisible(true);
    progressBar->setValue(0);

    QString targetDir =
        QDir(SettingsManager::sharedInstance()->devdiskimgpath())
            .filePath(version);
    if (!QDir().mkpath(targetDir)) {
        QMessageBox::critical(
            this, "Error",
            QString("Could not create directory: %1").arg(targetDir));
        downloadButton->setEnabled(true);
        progressBar->setVisible(false);
        return;
    }
    // todo is this safe ?
    auto *downloadItem = new DownloadItem();
    downloadItem->version = version;
    downloadItem->progressBar = progressBar;
    downloadItem->downloadButton = downloadButton;

    auto replies = DevDiskManager::sharedInstance()->downloadImage(version);
    downloadItem->dmgReply = replies.first;
    downloadItem->sigReply = replies.second;

    if (!downloadItem->dmgReply || !downloadItem->sigReply) {
        delete downloadItem;
        downloadButton->setEnabled(true);
        progressBar->setVisible(false);
        return;
    }

    connect(downloadItem->dmgReply, &QNetworkReply::downloadProgress, this,
            &DevDiskImagesWidget::onDownloadProgress);
    connect(downloadItem->dmgReply, &QNetworkReply::finished, this,
            &DevDiskImagesWidget::onFileDownloadFinished);
    connect(downloadItem->sigReply, &QNetworkReply::downloadProgress, this,
            &DevDiskImagesWidget::onDownloadProgress);
    connect(downloadItem->sigReply, &QNetworkReply::finished, this,
            &DevDiskImagesWidget::onFileDownloadFinished);

    m_activeDownloads[downloadItem->dmgReply] = downloadItem;
    m_activeDownloads[downloadItem->sigReply] = downloadItem;
}

void DevDiskImagesWidget::onDownloadProgress(qint64 bytesReceived,
                                             qint64 bytesTotal)
{
    auto *reply = qobject_cast<QNetworkReply *>(sender());
    if (!reply || !m_activeDownloads.contains(reply))
        return;

    auto *item = m_activeDownloads[reply];

    if (reply->property("totalSizeAdded").isNull() && bytesTotal > 0) {
        item->totalSize += bytesTotal;
        reply->setProperty("totalSizeAdded", true);
    }

    if (reply == item->dmgReply) {
        item->dmgReceived = bytesReceived;
    } else if (reply == item->sigReply) {
        item->sigReceived = bytesReceived;
    }

    item->totalReceived = item->dmgReceived + item->sigReceived;

    if (item->totalSize > 0) {
        item->progressBar->setValue((item->totalReceived * 100) /
                                    item->totalSize);
    }
}

// TODO: file saving should be in manager
void DevDiskImagesWidget::onFileDownloadFinished()
{
    auto *reply = qobject_cast<QNetworkReply *>(sender());
    if (!reply || !m_activeDownloads.contains(reply))
        return;

    auto *item = m_activeDownloads[reply];
    m_activeDownloads.remove(reply);

    if (reply->error() != QNetworkReply::NoError) {
        QMessageBox::critical(this, "Download Error",
                              QString("Failed to download %1: %2")
                                  .arg(reply->url().path())
                                  .arg(reply->errorString()));

        if (reply == item->dmgReply && item->sigReply)
            item->sigReply->abort();
        if (reply == item->sigReply && item->dmgReply)
            item->dmgReply->abort();

        item->downloadButton->setEnabled(true);
        item->downloadButton->setText("Retry");
        item->progressBar->setVisible(false);

        if (m_activeDownloads.key(item) == nullptr) {
            delete item;
        }
        reply->deleteLater();
        return;
    }

    QString path = QUrl::fromPercentEncoding(reply->url().path().toUtf8());
    QFileInfo fileInfo(path);
    QString filename = fileInfo.fileName();
    QString targetPath =
        QDir(QDir(SettingsManager::sharedInstance()->devdiskimgpath())
                 .filePath(item->version))
            .filePath(filename);

    QFile file(targetPath);
    if (!file.open(QIODevice::WriteOnly)) {
        QMessageBox::critical(
            this, "File Error",
            QString("Could not save file: %1").arg(targetPath));
    } else {
        file.write(reply->readAll());
        file.close();
    }

    reply->deleteLater();

    if (m_activeDownloads.key(item) == nullptr) { // Both files downloaded
        item->downloadButton->setText("Downloaded");
        item->downloadButton->setEnabled(false);
        item->progressBar->setValue(100);
        item->progressBar->setVisible(false);
        delete item;
    }
}

void DevDiskImagesWidget::updateDeviceList()
{
    auto devices = AppContext::sharedInstance()->getAllDevices();

    if (devices.isEmpty()) {
        m_currentDeviceUdid.clear();
        m_check_mountedButton->setEnabled(false);
        m_deviceComboBox->setEnabled(false);
    } else {
        m_deviceComboBox->setEnabled(true);
        m_check_mountedButton->setEnabled(true);
    }

    QString currentUdid = "";
    if (m_deviceComboBox->count() > 0 &&
        m_deviceComboBox->currentIndex() >= 0) {
        currentUdid = m_deviceComboBox->currentData().toString();
    } else if (!m_currentDeviceUdid.isEmpty()) {
        currentUdid = m_currentDeviceUdid;
    }

    m_deviceComboBox->clear();

    int newIndex = -1;
    for (int i = 0; i < devices.size(); ++i) {
        std::shared_ptr<iDescriptorDevice> device = devices.at(i);
        m_deviceComboBox->addItem(
            QString("%1 / (%2)")
                .arg(QString::fromStdString(device->deviceInfo.deviceName))
                .arg(QString::fromStdString(device->deviceInfo.productType)),
            device->udid);
        if (device->udid == currentUdid) {
            newIndex = i;
        }
    }

    if (newIndex != -1) {
        m_deviceComboBox->setCurrentIndex(newIndex);
    }
    displayImages();
}

void DevDiskImagesWidget::onMountButtonClicked()
{
    qDebug() << "Current index:" << m_deviceComboBox->currentIndex();
    if (m_deviceComboBox->currentIndex() < 0) {
        QMessageBox::warning(this, "No Device",
                             "Please select a device to mount the image on.");
        return;
    }

    auto *currentItem = m_imageListWidget->currentItem();
    if (!currentItem) {
        QMessageBox::warning(this, "No Image Selected",
                             "Please select a disk image to mount.");
        return;
    }

    auto *widget = m_imageListWidget->itemWidget(currentItem);
    auto *button = widget->findChild<QPushButton *>();
    if (!button)
        return;

    QString version = button->property("version").toString();

    this->mountImage(version);
}

void DevDiskImagesWidget::mountImage(const QString &version)
{
    QString udid = m_deviceComboBox->currentData().toString();
    m_deviceComboBox->setEnabled(false);

    if (udid.isEmpty()) {
        QMessageBox::warning(this, "No Device", "Please select a device.");
        return;
    }
    std::shared_ptr<iDescriptorDevice> device =
        AppContext::sharedInstance()->getDevice(udid);

    if (!device) {
        QMessageBox::warning(this, "Device Not Found",
                             "The selected device could not be found.");
        return;
    }

    auto *helper = new DevDiskImageHelper(device, this);
    connect(helper, &DevDiskImageHelper::finished, this, [this, helper]() {
        m_deviceComboBox->setEnabled(true);
        displayImages(); // Refresh
    });
    helper->mountVersion(version);
}

void DevDiskImagesWidget::closeEvent(QCloseEvent *event)
{
    if (!m_activeDownloads.isEmpty()) {
        auto reply = QMessageBox::question(
            this, "Downloads in Progress",
            QString(
                "There are %1 download(s) in progress. Do you really want to "
                "close and cancel all downloads?")
                .arg(m_activeDownloads.size()),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No);

        if (reply == QMessageBox::No) {
            event->ignore();
            return;
        }

        // Cancel all active downloads
        for (auto it = m_activeDownloads.begin(); it != m_activeDownloads.end();
             ++it) {
            QNetworkReply *reply = it.key();
            if (reply) {
                reply->abort();
            }
        }
    }

    event->accept();
}

void DevDiskImagesWidget::checkMountedImage()
{
    QString udid = m_deviceComboBox->currentData().toString();
    m_deviceComboBox->setEnabled(false);

    if (udid.isEmpty()) {
        QMessageBox::warning(this, "No Device", "Please select a device.");
        return;
    }
    qDebug() << "Checking mounted image for device UDID:" << udid;
    std::shared_ptr<iDescriptorDevice> device =
        AppContext::sharedInstance()->getDevice(udid);

    if (!device) {
        QMessageBox::warning(this, "Device Not Found",
                             "The selected device could not be found.");
        return;
    }

    connect(
        device->service_manager, &CXX::ServiceManager::mounted_image_retrieved,
        this,
        [this](bool success, bool locked, QByteArray signature,
               std::uint64_t sig_length) {
            m_deviceComboBox->setEnabled(true);
            if (!success) {
                if (locked) {
                    QMessageBox::warning(
                        this, "Device Locked",
                        "The device appears to be locked. Please unlock the "
                        "device and try again.");
                    return;
                }
                QMessageBox::critical(
                    this, "Error",
                    "Failed to retrieve mounted image information.");
                return;
            }

            if (signature.isEmpty() || sig_length == 0) {
                QMessageBox::information(
                    this, "No Image Mounted",
                    "There is currently no image mounted on the device.");
                m_mounted_sig.clear();
                m_mounted_sig_len = 0;
                displayImages(); // Refresh
                return;
            }

            QMessageBox::information(this, "Image Mounted",
                                     "A developer disk image is currently "
                                     "mounted on the device.");
            m_mounted_sig =
                std::string(reinterpret_cast<const char *>(signature.data()),
                            signature.size());
            m_mounted_sig_len = sig_length;
            displayImages(); // Refresh
        },
        Qt::SingleShotConnection);
    device->service_manager->get_mounted_image();
}
