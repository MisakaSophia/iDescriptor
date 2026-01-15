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

#ifndef GALLERYWIDGET_H
#define GALLERYWIDGET_H

#include "iDescriptor.h"
#include "photomodel.h"
#include "zloadingwidget.h"
#include <QWidget>

QT_BEGIN_NAMESPACE
class QListView;
class QComboBox;
class QPushButton;
class QHBoxLayout;
class QVBoxLayout;
class QStackedWidget;
class QLabel;
class QStandardItem;
QT_END_NAMESPACE

class ExportManager;
class ExportProgressDialog;

class GalleryWidget : public QWidget
{
    Q_OBJECT

public:
    explicit GalleryWidget(iDescriptorDevice *device,
                           QWidget *parent = nullptr);
    void load();
    ~GalleryWidget();

private slots:
    void onSortOrderChanged();
    void onFilterChanged();
    void onExportSelected();
    void onExportAll();
    void onAlbumSelected(const QString &albumPath);
    void onBackToAlbums();

private:
    void reload();
    void setupControlsLayout();
    void setupAlbumSelectionView();
    void setupPhotoGalleryView();
    void loadAlbumList(const AFCFileTree &dcimTree);
    void setControlsEnabled(bool enabled);
    QString selectExportDirectory();
    QIcon loadAlbumThumbnail(const QString &albumPath);
    void loadAlbumThumbnailAsync(const QString &albumPath, QStandardItem *item);
    void onPhotoContextMenu(const QPoint &pos);
    PhotoModel::FilterType getCurrentFilterType() const;

    iDescriptorDevice *m_device;
    bool m_loaded = false;
    QString m_currentAlbumPath;

    // UI components
    QVBoxLayout *m_mainLayout;
    QHBoxLayout *m_controlsLayout;
    ZLoadingWidget *m_loadingWidget;
    QPushButton *m_retryButton;
    QPushButton *m_importButton;
    // Album selection view
    QWidget *m_albumSelectionWidget;
    QListView *m_albumListView;

    // Photo gallery view
    QWidget *m_photoGalleryWidget;
    QListView *m_listView;
    PhotoModel *m_model;

    // Control widgets
    QComboBox *m_sortComboBox;
    QComboBox *m_filterComboBox;
    QPushButton *m_exportSelectedButton;
    QPushButton *m_exportAllButton;
    QPushButton *m_backButton;

    // Export manager
    ExportManager *m_exportManager;
    ExportProgressDialog *m_exportProgressDialog;
};

#endif // GALLERYWIDGET_H
