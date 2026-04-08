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
#include "imageloader.h"
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

PhotoModel::PhotoModel(const std::shared_ptr<iDescriptorDevice> device,
                       FilterType filterType, QObject *parent)
    : QAbstractListModel(parent), m_device(device), m_sortOrder(NewestFirst),
      m_filterType(filterType)
{
}

void PhotoModel::clear()
{
    QMutexLocker locker(&m_mutex);
    disconnect(&ImageLoader::sharedInstance(), &ImageLoader::thumbnailReady,
               this, &PhotoModel::onThumbnailReady);

    beginResetModel();
    m_photos.clear();
    m_allPhotos.clear();
    endResetModel();

    qDebug() << "Cleared PhotoModel data";
    // FIXME : bug we use the same loader on every device
    // FIXME: we shouldn't do this
    // QHashPrivate::Span<QHashPrivate::Node<QString, ImageTask *>>::hasNode
    // qhash.h         310  0x5555559d50e1
    // ImageLoader::sharedInstance().clear();
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

    case Qt::SizeHintRole:
        return QSize(210, 260);

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

        imgloader.requestThumbnail(m_device, info.filePath, index.row());

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
                                  unsigned int rowHint)
{
    Q_UNUSED(pixmap);

    QMutexLocker locker(&m_mutex);

    int row = -1;

    // if row hint still valid and matches this path
    if (rowHint < static_cast<unsigned int>(m_photos.size()) &&
        m_photos.at(static_cast<int>(rowHint)).filePath == path) {
        row = static_cast<int>(rowHint);
    } else {
        // fallback: search by path in current model
        for (int i = 0; i < m_photos.size(); ++i) {
            if (m_photos.at(i).filePath == path) {
                row = i;
                break;
            }
        }
    }

    if (row == -1) {
        // Thumbnail arrived for an item that is no longer in the model
        qDebug() << "PhotoModel::onThumbnailReady: path not in current model:"
                 << path << "(rowHint =" << rowHint
                 << ", size =" << m_photos.size() << ")";
        return;
    }

    QModelIndex idx = createIndex(row, 0);
    locker.unlock(); // avoid holding mutex while emitting
    emit dataChanged(idx, idx, {Qt::DecorationRole});
}

bool PhotoModel::populatePhotoPaths()
{
    // FIXME:DEADLOCK?
    // QMutexLocker locker(&m_mutex);
    connect(&ImageLoader::sharedInstance(), &ImageLoader::thumbnailReady, this,
            &PhotoModel::onThumbnailReady);
    if (m_albumPath.isEmpty()) {
        qDebug() << "No album path set, skipping population";
        return false;
    }

    m_allPhotos.clear();
    QMap<QString, QVariant> photoPaths =
        m_device->afc_backend->list_dir_with_creation_date(m_albumPath);

    for (auto it = photoPaths.constBegin(); it != photoPaths.constEnd(); ++it) {
        const QString &fileName = it.key();
        const QVariant &creationDateVariant = it.value();

        if (iDescriptor::Utils::isGalleryFile(fileName)) {
            PhotoInfo info;
            info.filePath = m_albumPath + "/" + fileName;
            info.fileName = fileName;
            info.thumbnailRequested = false;
            info.dateTime = creationDateVariant.toDateTime();
            info.fileType = determineFileType(fileName);
            m_allPhotos.append(info);
        }
    }

    // // Apply initial filtering and sorting, which will also reset the model
    applyFilterAndSort();

    qDebug() << "Loaded" << m_allPhotos.size() << "media files from device";
    qDebug() << "After filtering:" << m_photos.size() << "items shown";
    return true;
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
    QMutexLocker locker(&m_mutex);
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

PhotoInfo::FileType PhotoModel::determineFileType(const QString &fileName) const
{
    if (iDescriptor::Utils::isVideoFile(fileName))
        return PhotoInfo::Video;
    return PhotoInfo::Image;
}

int count = 0;
void PhotoModel::setAlbumPath(const QString &albumPath)
{
    qDebug() << "Setting new album path:" << albumPath;
    clear();

    m_albumPath = albumPath;
    QFutureWatcher<bool> *futureWatcher = new QFutureWatcher<bool>(this);
    QFuture<bool> future =
        QtConcurrent::run([this]() { return populatePhotoPaths(); });
    futureWatcher->setFuture(future);
    connect(futureWatcher, &QFutureWatcher<bool>::finished, this,
            [this, futureWatcher]() {
                futureWatcher->deleteLater();
                bool success = futureWatcher->result();
                if (success) {
                    qDebug() << "Finished populating photo paths for album:"
                             << m_albumPath;
                    emit albumPathSet();
                } else {
                    qDebug() << "Failed to populate photo paths for album:"
                             << m_albumPath;
                    emit albumPathSetFailed();
                }
            });
}
// TODO:REMOVE
void PhotoModel::refreshPhotos() { populatePhotoPaths(); }
