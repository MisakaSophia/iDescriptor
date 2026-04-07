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

#ifndef PHOTOMODEL_H
#define PHOTOMODEL_H

#include "iDescriptor.h"
#include <QAbstractListModel>
#include <QCache>
#include <QCryptographicHash>
#include <QDateTime>
#include <QFutureWatcher>
#include <QMutex>
#include <QMutexLocker>
#include <QPixmap>
#include <QSize>
#include <QStandardPaths>
#include <QtConcurrent/QtConcurrent>

struct PhotoInfo {
    QString filePath;
    QString fileName;
    QDateTime dateTime;
    bool thumbnailRequested = false;

    enum FileType { Image, Video };
    FileType fileType;
};

class PhotoModel : public QAbstractListModel
{
    Q_OBJECT

public:
    enum SortOrder { NewestFirst, OldestFirst };

    enum FilterType { All, ImagesOnly, VideosOnly };

    explicit PhotoModel(const std::shared_ptr<iDescriptorDevice> device,
                        FilterType filterType, QObject *parent = nullptr);
    ~PhotoModel();

    // QAbstractItemModel interface
    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index,
                  int role = Qt::DisplayRole) const override;

    // Album management
    void setAlbumPath(const QString &albumPath);
    void refreshPhotos();

    // Sorting and filtering
    void setSortOrder(SortOrder order);
    SortOrder sortOrder() const { return m_sortOrder; }

    void setFilterType(FilterType filter);
    FilterType filterType() const { return m_filterType; }

    // Export functionality
    QStringList getSelectedFilePaths(const QModelIndexList &indexes) const;
    QString getFilePath(const QModelIndex &index) const;
    PhotoInfo::FileType getFileType(const QModelIndex &index) const;

    // Get all items for export
    QStringList getAllFilePaths() const;
    QStringList getFilteredFilePaths() const;

    void clear();

private:
    const std::shared_ptr<iDescriptorDevice> m_device;
    QString m_albumPath;
    QList<PhotoInfo> m_allPhotos; // All photos from device
    QList<PhotoInfo> m_photos;    // Currently filtered/sorted photos

    // Sorting and filtering
    SortOrder m_sortOrder;
    FilterType m_filterType;

    QMutex m_mutex;

    // Helper methods
    bool populatePhotoPaths();
    void applyFilterAndSort();
    void sortPhotos(QList<PhotoInfo> &photos) const;
    bool matchesFilter(const PhotoInfo &info) const;

    PhotoInfo::FileType determineFileType(const QString &fileName) const;

private slots:
    void onThumbnailReady(const QString &path, const QPixmap &pixmap,
                          unsigned int row);

signals:
    void albumPathSet();
    void albumPathSetFailed();
};

#endif // PHOTOMODEL_H