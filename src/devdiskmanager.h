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

#ifndef DEVDISKMANAGER_H
#define DEVDISKMANAGER_H

#include "iDescriptor.h"
#include <QMap>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QObject>
#include <QStringList>

class DevDiskManager : public QObject
{
    Q_OBJECT
public:
    explicit DevDiskManager(QObject *parent = nullptr);
    static DevDiskManager *sharedInstance();

    QList<ImageInfo> parseImageList(QString path, int deviceMajorVersion,
                                    int deviceMinorVersion,
                                    const char *mounted_sig,
                                    uint64_t mounted_sig_len);
    QList<ImageInfo> getAllImages() const;

    // Download management
    QPair<QNetworkReply *, QNetworkReply *>
    downloadImage(const QString &version);
    bool isImageDownloaded(const QString &version,
                           const QString &downloadPath) const;

    // Mount operations

    bool mountImage(const QString &version, const iDescriptorDevice *device);
    bool unmountImage();
    std::pair<QString, QString> getPathsForVersion(const QString &version);

    // Signature comparison
    bool compareSignatures(const char *signature_file_path,
                           const char *mounted_sig, uint64_t mounted_sig_len);

    QByteArray getImageListData() const { return m_imageListJsonData; }
    bool mountCompatibleImage(const iDescriptorDevice *device);
    QString downloadCompatibleImage(
        const std::shared_ptr<iDescriptorDevice> device,
        std::function<void(bool, const QString &version)> callback);

signals:
    void imageListFetched(bool success,
                          const QString &errorMessage = QString());
    void imageDownloadProgress(const QString &version, int percentage);
    void imageDownloadFinished(const QString &version, bool success,
                               const QString &errorMessage = QString());

private slots:
    void onDownloadProgress(qint64 bytesReceived, qint64 bytesTotal);
    void onFileDownloadFinished();

private:
    struct DownloadItem {
        QString version;
        QString downloadPath;
        QNetworkReply *dmgReply = nullptr;
        QNetworkReply *sigReply = nullptr;
        qint64 dmgReceived = 0;
        qint64 sigReceived = 0;
        qint64 totalSize = 0;
    };

    QNetworkAccessManager *m_networkManager;
    QByteArray m_imageListJsonData;
    QMap<QString, ImageInfo> m_availableImages;
    QMap<QNetworkReply *, DownloadItem *> m_activeDownloads;

    QMap<QString, QMap<QString, QString>> parseDiskDir();
    QList<ImageInfo>
    getImagesSorted(QMap<QString, QMap<QString, QString>> imageFiles,
                    QString path, int deviceMajorVersion,
                    int deviceMinorVersion, const char *mounted_sig,
                    uint64_t mounted_sig_len);
    void populateImageList();
};

#endif // DEVDISKMANAGER_H
