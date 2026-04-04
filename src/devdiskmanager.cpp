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

#include "devdiskmanager.h"
#include "iDescriptor.h"
#include "settingsmanager.h"
#include <QApplication>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>

DevDiskManager *DevDiskManager::sharedInstance()
{
    static DevDiskManager instance;
    return &instance;
}

DevDiskManager::DevDiskManager(QObject *parent) : QObject{parent}
{
    m_networkManager = new QNetworkAccessManager(this);
    populateImageList();
}

/*
 * if we have DeveloperDiskImages.json in docs read from there if not populate
 * with the file in resources and then try to update it
 */
void DevDiskManager::populateImageList()
{
    QString localPath = QDir(SettingsManager::sharedInstance()->homePath())
                            .filePath("DeveloperDiskImages.json");
    qDebug() << "Looking for DeveloperDiskImages.json at" << localPath;
    QFile localFile(localPath);

    if (localFile.exists() && localFile.open(QIODevice::ReadOnly)) {
        m_imageListJsonData = localFile.readAll();
        localFile.close();
        qDebug() << "Loaded DeveloperDiskImages.json from local cache.";
    } else {
        QFile qrcFile(":/DeveloperDiskImages.json");
        if (qrcFile.open(QIODevice::ReadOnly)) {
            m_imageListJsonData = qrcFile.readAll();
            qrcFile.close();
            qDebug() << "Loaded DeveloperDiskImages.json from QRC resources.";
        } else {
            qWarning()
                << "Could not open DeveloperDiskImages.json from QRC. "
                   "Image list will be empty until network fetch succeeds.";
        }
    }
    QUrl url(DEVELOPER_DISK_IMAGE_JSON_URL);
    QNetworkRequest request(url);
    auto *reply = m_networkManager->get(request);

    connect(reply, &QNetworkReply::finished, this, [this, localPath, reply]() {
        if (reply->error() == QNetworkReply::NoError) {
            // FIXME: better have this in settings
            QDir().mkdir(QDir::homePath() + "/.idescriptor");
            m_imageListJsonData = reply->readAll();
            QFile file(localPath);
            if (file.open(QIODevice::WriteOnly)) {
                file.write(m_imageListJsonData);
                file.close();
            }
        }
        reply->deleteLater();
    });
}

QMap<QString, QMap<QString, QString>> DevDiskManager::parseDiskDir()
{
    QJsonDocument doc = QJsonDocument::fromJson(m_imageListJsonData);
    if (!doc.isObject()) {
        qWarning() << "parseDiskDir: Invalid JSON response from image list API";
        return {};
    }

    QMap<QString, QMap<QString, QString>>
        imageFiles; // version -> {type -> url}

    QJsonObject root = getVersionedConfig(doc.object());
    if (root.isEmpty()) {
        qWarning() << "parseDiskDir: No valid versioned config found in image "
                      "list JSON";
        return {};
    }
    for (auto it = root.constBegin(); it != root.constEnd(); ++it) {
        const QString version = it.key();
        const QJsonObject versionData = it.value().toObject();

        // Skip special entries
        if (version == "Fallback") {
            continue;
        }

        QMap<QString, QString> versionFiles;

        // Handle Image URLs
        if (versionData.contains("Image")) {
            QJsonArray imageArray = versionData["Image"].toArray();
            if (!imageArray.isEmpty()) {
                versionFiles["DeveloperDiskImage.dmg"] =
                    imageArray[0].toString();
            }
        }

        // Handle Signature URLs
        if (versionData.contains("Signature")) {
            QJsonArray sigArray = versionData["Signature"].toArray();
            if (!sigArray.isEmpty()) {
                versionFiles["DeveloperDiskImage.dmg.signature"] =
                    sigArray[0].toString();
            }
        }

        // Only add versions that have at least an image file
        if (!versionFiles.isEmpty() &&
            versionFiles.contains("DeveloperDiskImage.dmg")) {
            imageFiles[version] = versionFiles;
        }
    }

    return imageFiles;
}

QList<ImageInfo> DevDiskManager::parseImageList(QString path,
                                                int deviceMajorVersion,
                                                int deviceMinorVersion,
                                                const char *mounted_sig,
                                                uint64_t mounted_sig_len)
{
    m_availableImages.clear();

    QMap<QString, QMap<QString, QString>> imageFiles = parseDiskDir();
    QList<ImageInfo> sortedResult =
        getImagesSorted(imageFiles, path, deviceMajorVersion,
                        deviceMinorVersion, mounted_sig, mounted_sig_len);

    return sortedResult;
}

QList<ImageInfo> DevDiskManager::getImagesSorted(
    QMap<QString, QMap<QString, QString>> imageFiles, QString path,
    int deviceMajorVersion, int deviceMinorVersion, const char *mounted_sig,
    uint64_t mounted_sig_len)
{
    QList<ImageInfo> allImages;
    // FIXME: i guess we could do better here but works for now
    bool hasConnectedDevice = (deviceMajorVersion > 0);

    for (auto it = imageFiles.constBegin(); it != imageFiles.constEnd(); ++it) {
        if (it.value().contains("DeveloperDiskImage.dmg") &&
            it.value().contains("DeveloperDiskImage.dmg.signature")) {
            QString version = it.key();

            ImageInfo info;
            info.version = version;
            info.dmgPath = it.value()["DeveloperDiskImage.dmg"];
            info.sigPath = it.value()["DeveloperDiskImage.dmg.signature"];
            info.isDownloaded = isImageDownloaded(version, path);

            // Determine compatibility
            if (hasConnectedDevice) {
                QStringList versionParts = version.split('.');
                if (versionParts.size() >= 1) {
                    bool ma_ok;
                    bool mi_ok;
                    int imageMajorVersion = versionParts[0].toInt(&ma_ok);
                    int imageMinorVersion = (versionParts.size() >= 2)
                                                ? versionParts[1].toInt(&mi_ok)
                                                : 0;

                    if (ma_ok && mi_ok) {

                        // FIXME: this seems to work only for older iphones
                        // so commented out but in the future , it may be
                        // enabled
                        if (imageMajorVersion == deviceMajorVersion) {
                            if (imageMinorVersion == deviceMinorVersion) {
                                // Exact match
                                info.compatibility =
                                    ImageCompatibility::Compatible;
                            } else {
                                // Major matches but minor doesn't
                                info.compatibility =
                                    ImageCompatibility::MaybeCompatible;
                            }
                        } else {
                            info.compatibility =
                                ImageCompatibility::NotCompatible;
                        }
                    }
                }
            }

            // Check if mounted
            /*
                in my testing some ios versions do accept older minor versions
               as well for example an iPhone 5s with iOS 12.5 accepts 12.4 but
               newer iPhones are more strict, so lets just check where it's
               compatible or not
            */
            // if (info.isCompatible && info.isDownloaded && mounted_sig)
            if (info.isDownloaded && mounted_sig) {
                QString sigLocalPath =
                    QDir(
                        QDir(
                            SettingsManager::sharedInstance()->devdiskimgpath())
                            .filePath(version))
                        .filePath("DeveloperDiskImage.dmg.signature");
                info.isMounted =
                    compareSignatures(sigLocalPath.toUtf8().constData(),
                                      mounted_sig, mounted_sig_len);
            }

            m_availableImages[version] = info;
            allImages.append(info);
        }
    }

    // Sort images: Compatible first, then MaybeCompatible, then NotCompatible
    // Within each group, sort by version (newest first)
    auto versionSort = [](const ImageInfo &a, const ImageInfo &b) {
        // First sort by compatibility
        if (a.compatibility != b.compatibility) {
            return a.compatibility <
                   b.compatibility; // Compatible(0) < MaybeCompatible(1) <
                                    // NotCompatible(2)
        }

        // Then sort by version (newest first)
        QStringList aParts = a.version.split('.');
        QStringList bParts = b.version.split('.');

        for (int i = 0; i < qMax(aParts.size(), bParts.size()); ++i) {
            int aNum = (i < aParts.size()) ? aParts[i].toInt() : 0;
            int bNum = (i < bParts.size()) ? bParts[i].toInt() : 0;

            if (aNum != bNum) {
                return aNum > bNum; // Descending order (newest first)
            }
        }
        return false;
    };

    std::sort(allImages.begin(), allImages.end(), versionSort);

    return allImages;
}

QList<ImageInfo> DevDiskManager::getAllImages() const
{
    return m_availableImages.values();
}

QPair<QNetworkReply *, QNetworkReply *>
DevDiskManager::downloadImage(const QString &version)
{
    qDebug() << "Request to download image version:" << version;
    if (!m_availableImages.contains(version)) {
        qDebug() << "Image not found:" << version;
        emit imageDownloadFinished(version, false, "Image version not found.");
        return {nullptr, nullptr};
    }

    QString targetDir =
        QDir(SettingsManager::sharedInstance()->devdiskimgpath())
            .filePath(version);
    if (!QDir().mkpath(targetDir)) {
        qDebug() << "Could not create directory:" << targetDir;
        emit imageDownloadFinished(
            version, false,
            QString("Could not create directory: %1").arg(targetDir));
        return {nullptr, nullptr};
    }

    const ImageInfo &info = m_availableImages[version];

    QUrl dmgUrl(info.dmgPath);
    QNetworkRequest dmgRequest(dmgUrl);
    QNetworkReply *dmgReply = m_networkManager->get(dmgRequest);

    QUrl sigUrl(info.sigPath);
    QNetworkRequest sigRequest(sigUrl);
    QNetworkReply *sigReply = m_networkManager->get(sigRequest);

    return {dmgReply, sigReply};
}

bool DevDiskManager::isImageDownloaded(const QString &version,
                                       const QString &downloadPath) const
{
    QString versionPath = QDir(downloadPath).filePath(version);
    QString dmgPath = QDir(versionPath).filePath("DeveloperDiskImage.dmg");
    QString sigPath =
        QDir(versionPath).filePath("DeveloperDiskImage.dmg.signature");

    return QFile::exists(dmgPath) && QFile::exists(sigPath);
}

QString DevDiskManager::downloadCompatibleImage(
    const std::shared_ptr<iDescriptorDevice> device,
    std::function<void(bool, const QString &)> callback)
{
    QString path = SettingsManager::sharedInstance()->mkDevDiskImgPath();
    unsigned int deviceMajorVersion =
        device->deviceInfo.parsedDeviceVersion.major;
    unsigned int deviceMinorVersion =
        device->deviceInfo.parsedDeviceVersion.minor;
    qDebug() << "Device version:" << deviceMajorVersion << "."
             << deviceMinorVersion;
    QList<ImageInfo> images =
        parseImageList(path, deviceMajorVersion, deviceMinorVersion, "", 0);

    if (images.isEmpty()) {
        qDebug() << "No images found for device version:" << deviceMajorVersion
                 << "." << deviceMinorVersion;
        callback(false, "");
        return "";
    }

    for (const ImageInfo &info : images) {
        if (info.compatibility != ImageCompatibility::Compatible &&
            info.compatibility != ImageCompatibility::MaybeCompatible) {
            continue;
        }
        if (info.isDownloaded) {
            callback(true, info.version);
            return info.version;
        }
    }

    // If none are downloaded, download the newest compatible one
    for (const ImageInfo &info : images) {
        if (info.compatibility == ImageCompatibility::Compatible ||
            info.compatibility == ImageCompatibility::MaybeCompatible) {
            const QString versionToDownload = info.version;
            connect(
                this, &DevDiskManager::imageDownloadFinished, this,
                [this, versionToDownload,
                 callback](const QString &finishedVersion, bool success,
                           const QString &errorMessage) {
                    if (finishedVersion == versionToDownload) {
                        callback(success, versionToDownload);
                    }
                },
                Qt::SingleShotConnection);

            qDebug()
                << "No compatible image found locally. Downloading version:"
                << versionToDownload;

            QPair<QNetworkReply *, QNetworkReply *> replies =
                downloadImage(versionToDownload);
            auto *downloadItem = new DownloadItem();
            downloadItem->version = versionToDownload;
            downloadItem->downloadPath = path;
            downloadItem->dmgReply = replies.first;
            downloadItem->sigReply = replies.second;

            connect(downloadItem->dmgReply, &QNetworkReply::downloadProgress,
                    this, &DevDiskManager::onDownloadProgress);
            connect(downloadItem->dmgReply, &QNetworkReply::finished, this,
                    &DevDiskManager::onFileDownloadFinished);
            connect(downloadItem->sigReply, &QNetworkReply::downloadProgress,
                    this, &DevDiskManager::onDownloadProgress);
            connect(downloadItem->sigReply, &QNetworkReply::finished, this,
                    &DevDiskManager::onFileDownloadFinished);

            m_activeDownloads[downloadItem->dmgReply] = downloadItem;
            m_activeDownloads[downloadItem->sigReply] = downloadItem;
            return versionToDownload;
        }
    }

    qDebug() << "No compatible image found to mount on device:" << device->udid;

    return "";
}

std::pair<QString, QString>
DevDiskManager::getPathsForVersion(const QString &version)
{
    const QString downloadPath =
        SettingsManager::sharedInstance()->devdiskimgpath();
    QString versionPath = QDir(downloadPath).filePath(version);
    QString dmgPath = QDir(versionPath).filePath("DeveloperDiskImage.dmg");
    QString sigPath =
        QDir(versionPath).filePath("DeveloperDiskImage.dmg.signature");

    return {dmgPath, sigPath};
}

void DevDiskManager::onDownloadProgress(qint64 bytesReceived, qint64 bytesTotal)
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

    qint64 totalReceived = item->dmgReceived + item->sigReceived;

    if (item->totalSize > 0) {
        emit imageDownloadProgress(item->version,
                                   (totalReceived * 100) / item->totalSize);
    }
}

void DevDiskManager::onFileDownloadFinished()
{
    auto *reply = qobject_cast<QNetworkReply *>(sender());
    if (!reply || !m_activeDownloads.contains(reply))
        return;

    auto *item = m_activeDownloads[reply];
    m_activeDownloads.remove(reply);

    if (reply->error() != QNetworkReply::NoError) {
        emit imageDownloadFinished(item->version, false, reply->errorString());

        if (reply == item->dmgReply && item->sigReply)
            item->sigReply->abort();
        if (reply == item->sigReply && item->dmgReply)
            item->dmgReply->abort();

        if (m_activeDownloads.key(item) == nullptr) {
            delete item;
        }
        reply->deleteLater();
        return;
    }

    QString path = QUrl::fromPercentEncoding(reply->url().path().toUtf8());
    QFileInfo fileInfo(path);
    QString filename = fileInfo.fileName();
    QString targetPath = QDir(QDir(item->downloadPath).filePath(item->version))
                             .filePath(filename);

    QFile file(targetPath);
    qDebug() << "Saving downloaded file to:" << targetPath;
    if (!file.open(QIODevice::WriteOnly)) {
        emit imageDownloadFinished(
            item->version, false,
            QString("Could not save file: %1").arg(targetPath));
    } else {
        file.write(reply->readAll());
        file.close();
    }

    reply->deleteLater();

    if (m_activeDownloads.key(item) == nullptr) { // Both files finished
        emit imageDownloadFinished(item->version, true);
        delete item;
    }
}

bool DevDiskManager::compareSignatures(const char *signature_file_path,
                                       const char *mounted_sig,
                                       uint64_t mounted_sig_len)
{
    FILE *f_sig = fopen(signature_file_path, "rb");
    if (!f_sig) {
        qDebug() << "ERROR: Could not open signature file:"
                 << signature_file_path;
        return false;
    }

    fseek(f_sig, 0, SEEK_END);
    long local_sig_len = ftell(f_sig);
    fseek(f_sig, 0, SEEK_SET);

    char *local_sig = (char *)malloc(local_sig_len);
    if (!local_sig) {
        fclose(f_sig);
        return false;
    }

    fread(local_sig, 1, local_sig_len, f_sig);
    fclose(f_sig);

    bool matches = false;
    if ((mounted_sig_len == (uint64_t)local_sig_len) &&
        (memcmp(mounted_sig, local_sig, mounted_sig_len) == 0)) {
        qDebug() << "Signatures match!";
        matches = true;
    } else {
        qDebug() << "Signatures DO NOT match!";
    }

    free(local_sig);
    return matches;
}