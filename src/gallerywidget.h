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

#include "exportalbum.h"
#include "iDescriptor-ui.h"
#include "iDescriptor.h"
#include "photoimportdialog.h"
#include "photomodel.h"
#include "zloadingwidget.h"
#include <QStringList>
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
class QStandardItemModel;
QT_END_NAMESPACE

class ExportManager;

class GalleryWidget : public QWidget
{
    Q_OBJECT

public:
    explicit GalleryWidget(const std::shared_ptr<iDescriptorDevice> device,
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
    void refresh();
    void setupControlsLayout();
    void setupAlbumSelectionView();
    void setupPhotoGalleryView();
    void onAlbumListLoaded(const QList<QString> &dcimTree);
    void setControlsEnabled(bool enabled);
    QString selectExportDirectory();
    static QImage loadAlbumThumbnail(const QString &albumPath,
                                     std::shared_ptr<iDescriptorDevice> device);
    void loadAlbumThumbnailAsync(const QString &albumPath, QStandardItem *item);
    void onPhotoContextMenu(const QPoint &pos);
    PhotoModel::FilterType getCurrentFilterType() const;
    void handleImport();
    void onError();

    const std::shared_ptr<iDescriptorDevice> m_device;
    bool m_loaded = false;
    QString m_currentAlbumPath;

    // UI components
    QVBoxLayout *m_mainLayout;
    QHBoxLayout *m_controlsLayout;
    ZLoadingWidget *m_loadingWidget;
    QPushButton *m_importButton;

    // Album selection view
    QWidget *m_albumSelectionWidget = nullptr;
    QListView *m_albumListView = nullptr;

    // Photo gallery view
    QWidget *m_photoGalleryWidget = nullptr;
    QListView *m_listView = nullptr;
    PhotoModel *m_model = nullptr;
    QStandardItemModel *m_albumModel;

    // Control widgets
    QComboBox *m_sortComboBox;
    QComboBox *m_filterComboBox;
    QPushButton *m_exportSelectedButton;
    QPushButton *m_exportAllButton;
    ZIconWidget *m_backButton = nullptr;
    ZIconWidget *m_refreshButton = nullptr;

    // Export manager
    ExportManager *m_exportManager;
};

#endif // GALLERYWIDGET_H
