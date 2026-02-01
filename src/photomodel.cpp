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

#include "photomodel.h"
#include "iDescriptor.h"
// #include "mediastreamermanager.h"
#include "imageloader.h"
#include "servicemanager.h"
#include <QDebug>
#include <QEventLoop>
#include <QIcon>
#include <QImage>
#include <QImageReader>
#include <QMediaPlayer>
#include <QPixmap>
#include <QRegularExpression>
#include <QSemaphore>
#include <QTimer>
#include <QVideoFrame>
#include <QVideoSink>
#include <QtConcurrent/QtConcurrent>

PhotoModel::PhotoModel(iDescriptorDevice *device, FilterType filterType,
                       QObject *parent)
    : QAbstractListModel(parent), m_device(device), m_thumbnailSize(120, 120),
      m_sortOrder(NewestFirst), m_filterType(filterType)
{
    connect(&ImageLoader::sharedInstance(), &ImageLoader::thumbnailReady, this,
            &PhotoModel::onThumbnailReady);
}

void PhotoModel::clear()
{
    blockSignals(true);

    // // Clean up any active watchers
    // for (auto *watcher : m_activeLoaders.values()) {
    //     if (watcher) {
    //         watcher->disconnect();
    //         watcher->cancel();
    //         // watcher->waitForFinished();
    //         watcher->deleteLater();
    //     }
    // }
    // m_activeLoaders.clear();
    // m_loadingPaths.clear();
    // m_thumbnailCache.clear();

    beginResetModel();
    m_photos.clear();
    m_allPhotos.clear();
    endResetModel();

    blockSignals(false);
}

PhotoModel::~PhotoModel()
{
    qDebug() << "PhotoModel destructor called";
    clear();
}

int PhotoModel::rowCount(const QModelIndex &parent) const
{
    Q_UNUSED(parent)
    return m_photos.size();
}

QVariant PhotoModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= m_photos.size())
        return QVariant();

    const PhotoInfo &info = m_photos.at(index.row());

    switch (role) {
    case Qt::DisplayRole:
        return info.fileName;

    case Qt::UserRole:
        return info.filePath;

    case Qt::DecorationRole: {
        ImageLoader &imgloader = ImageLoader::sharedInstance();
        // Check memory cache first
        if (QPixmap *cached = imgloader.m_cache.object(info.filePath)) {
            return QIcon(*cached);
        }

        if (imgloader.isLoading(info.filePath)) {
            if (iDescriptor::Utils::isVideoFile(info.fileName)) {
                return QIcon(":/resources/icons/video-x-generic.png");
            } else {
                return QIcon(":/resources/icons/"
                             "MaterialSymbolsLightImageOutlineSharp.png");
            }
        }

        imgloader.requestThumbnail(m_device, info.filePath, index.row(),
                                   index.row());

        if (iDescriptor::Utils::isVideoFile(info.fileName)) {
            return QIcon(":/resources/icons/video-x-generic.png");
        } else {
            return QIcon(
                ":/resources/icons/MaterialSymbolsLightImageOutlineSharp.png");
        }
    }

    case Qt::ToolTipRole:
        return QString("Photo: %1").arg(info.fileName);

    default:
        return QVariant();
    }
}

void PhotoModel::onThumbnailReady(const QString &path, const QPixmap &pixmap,
                                  unsigned int row)
{
    if (m_photos[row].filePath == path) {
        QModelIndex idx = createIndex(row, 0);
        emit dataChanged(idx, idx, {Qt::DecorationRole});
    }
}

void PhotoModel::populatePhotoPaths()
{
    // TODO:beginResetModel called on PhotoModel(0x600002d12a40) without calling
    // endResetModel first
    if (m_albumPath.isEmpty()) {
        qDebug() << "No album path set, skipping population";
        return;
    }

    m_allPhotos.clear();

    QByteArray albumPathBytes = m_albumPath.toUtf8();
    const char *albumPathCStr = albumPathBytes.constData();

    AfcFileInfo albumInfo = {};

    IdeviceFfiError *err =
        ServiceManager::safeAfcGetFileInfo(m_device, albumPathCStr, &albumInfo);
    if (err) {
        qDebug() << "Album path does not exist or cannot be accessed:"
                 << m_albumPath << "Error:" << err->message;
        idevice_error_free(err);
        return;
    }
    // FIXME: should we continue if albumInfo is null?
    if (albumInfo.size) {
        afc_file_info_free(&albumInfo);
    }

    // Fix: Store the QByteArray to keep the C string valid
    QByteArray photoDirBytes = m_albumPath.toUtf8();
    const char *photoDir = photoDirBytes.constData();
    qDebug() << "Photo directory:" << m_albumPath;
    qDebug() << "Photo directory C string:" << photoDir;

    char **files = nullptr;
    size_t count = 0;
    err =
        ServiceManager::safeAfcReadDirectory(m_device, photoDir, &files, count);
    if (err) {
        qDebug() << "Failed to read photo directory:" << photoDir
                 << "Error:" << err->message;
        idevice_error_free(err);
        return;
    }

    if (files) {
        for (int i = 0; files[i]; i++) {
            QString fileName = QString::fromUtf8(files[i]);
            if (fileName.endsWith(".JPG", Qt::CaseInsensitive) ||
                fileName.endsWith(".PNG", Qt::CaseInsensitive) ||
                fileName.endsWith(".HEIC", Qt::CaseInsensitive) ||
                fileName.endsWith(".MOV", Qt::CaseInsensitive) ||
                fileName.endsWith(".MP4", Qt::CaseInsensitive) ||
                fileName.endsWith(".M4V", Qt::CaseInsensitive)) {

                PhotoInfo info;
                info.filePath = m_albumPath + "/" + fileName;
                info.fileName = fileName;
                info.thumbnailRequested = false;
                info.fileType = determineFileType(fileName);
                info.dateTime = extractDateTimeFromFile(info.filePath);

                m_allPhotos.append(info);
            }
        }
        free_directory_listing(files, count);
    }

    // Apply initial filtering and sorting, which will also reset the model
    applyFilterAndSort();

    qDebug() << "Loaded" << m_allPhotos.size() << "media files from device";
    qDebug() << "After filtering:" << m_photos.size() << "items shown";
}

// Sorting and filtering methods
void PhotoModel::setSortOrder(SortOrder order)
{
    if (m_sortOrder != order) {
        m_sortOrder = order;
        applyFilterAndSort();
    }
}

void PhotoModel::setFilterType(FilterType filter)
{
    if (m_filterType != filter) {
        m_filterType = filter;
        applyFilterAndSort();
    }
}

void PhotoModel::applyFilterAndSort()
{
    beginResetModel();

    // Filter photos
    m_photos.clear();
    for (const PhotoInfo &info : m_allPhotos) {
        if (matchesFilter(info)) {
            m_photos.append(info);
        }
    }

    // Sort photos
    sortPhotos(m_photos);

    endResetModel();

    qDebug() << "Applied filter and sort - showing" << m_photos.size() << "of"
             << m_allPhotos.size() << "items";
}

void PhotoModel::sortPhotos(QList<PhotoInfo> &photos) const
{
    std::sort(photos.begin(), photos.end(),
              [this](const PhotoInfo &a, const PhotoInfo &b) {
                  if (m_sortOrder == NewestFirst) {
                      return a.dateTime > b.dateTime;
                  } else {
                      return a.dateTime < b.dateTime;
                  }
              });
}

bool PhotoModel::matchesFilter(const PhotoInfo &info) const
{
    switch (m_filterType) {
    case All:
        return true;
    case ImagesOnly:
        return info.fileType == PhotoInfo::Image;
    case VideosOnly:
        return info.fileType == PhotoInfo::Video;
    default:
        return true;
    }
}

// Export functionality
QStringList
PhotoModel::getSelectedFilePaths(const QModelIndexList &indexes) const
{
    QStringList paths;
    for (const QModelIndex &index : indexes) {
        if (index.isValid() && index.row() < m_photos.size()) {
            paths.append(m_photos.at(index.row()).filePath);
        }
    }
    return paths;
}

QString PhotoModel::getFilePath(const QModelIndex &index) const
{
    if (index.isValid() && index.row() < m_photos.size()) {
        return m_photos.at(index.row()).filePath;
    }
    return QString();
}

PhotoInfo::FileType PhotoModel::getFileType(const QModelIndex &index) const
{
    if (index.isValid() && index.row() < m_photos.size()) {
        return m_photos.at(index.row()).fileType;
    }
    return PhotoInfo::Image;
}

QStringList PhotoModel::getAllFilePaths() const
{
    QStringList paths;
    for (const PhotoInfo &info : m_allPhotos) {
        paths.append(info.filePath);
    }
    return paths;
}

QStringList PhotoModel::getFilteredFilePaths() const
{
    QStringList paths;
    for (const PhotoInfo &info : m_photos) {
        paths.append(info.filePath);
    }
    return paths;
}

// Helper methods
QDateTime PhotoModel::extractDateTimeFromFile(const QString &filePath) const
{
    AfcFileInfo *info = nullptr;
    IdeviceFfiError *err = ServiceManager::safeAfcGetFileInfo(
        m_device, filePath.toUtf8().constData(), info);
    if (!err && info) {
        uint64_t creation_seconds = info->creation;
        QDateTime dateTime =
            QDateTime::fromSecsSinceEpoch(creation_seconds, Qt::UTC);

        // afc_file_info_free(info);
        if (dateTime.isValid()) {
            return dateTime;
        }
    }

    return QDateTime::currentDateTime();
}

PhotoInfo::FileType PhotoModel::determineFileType(const QString &fileName) const
{
    if (iDescriptor::Utils::isVideoFile(fileName)) {
        return PhotoInfo::Video;
    } else {
        return PhotoInfo::Image;
    }
}

void PhotoModel::setAlbumPath(const QString &albumPath)
{
    qDebug() << "Setting new album path:" << albumPath;
    clear();

    m_albumPath = albumPath;
    populatePhotoPaths();
}

void PhotoModel::refreshPhotos() { populatePhotoPaths(); }
