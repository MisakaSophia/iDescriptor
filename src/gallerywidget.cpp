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

#include "gallerywidget.h"
#include "exportmanager.h"
#include "iDescriptor-ui.h"
#include "iDescriptor.h"
#include "mediapreviewdialog.h"
#include "photomodel.h"
#include "servicemanager.h"
#include <QComboBox>
#include <QDebug>
#include <QFileDialog>
#include <QFutureWatcher>
#include <QHBoxLayout>
#include <QItemSelectionModel>
#include <QLabel>
#include <QListView>
#include <QMenu>
#include <QMessageBox>
#include <QPushButton>
#include <QRegularExpression>
#include <QStackedWidget>
#include <QStandardItemModel>
#include <QStandardPaths>
#include <QStringList>
#include <QVBoxLayout>
#include <QtConcurrent/QtConcurrent>

// todo: dont load paths on main thread, handle
/*
    FIXME: this needs to be refactored once we
    figure out how to query Photos.sqlite
    Check out:
    https://github.com/ScottKjr3347/iOS_Local_PL_Photos.sqlite_Queries
*/

GalleryWidget::GalleryWidget(const iDescriptorDevice *device, QWidget *parent)
    : QWidget{parent}, m_device(device), m_model(nullptr),
      m_albumSelectionWidget(nullptr), m_albumListView(nullptr),
      m_photoGalleryWidget(nullptr), m_listView(nullptr), m_backButton(nullptr)
{
    m_mainLayout = new QVBoxLayout(this);
    m_mainLayout->setContentsMargins(0, 0, 0, 0);
    m_loadingWidget = new ZLoadingWidget(true, this);
    setupControlsLayout();
    m_mainLayout->addWidget(m_loadingWidget);

    // Setup album selection view
    setupAlbumSelectionView();

    // Setup photo gallery view
    setupPhotoGalleryView();

    // Add stacked widget to main layout
    setLayout(m_mainLayout);

    QVBoxLayout *errorLayout = new QVBoxLayout();
    errorLayout->setAlignment(Qt::AlignCenter);
    QLabel *errorLabel = new QLabel("Failed to load albums.");
    errorLabel->setStyleSheet("font-weight: bold; color: red;");
    errorLayout->addWidget(errorLabel);
    m_retryButton = new QPushButton("Retry", this);
    errorLayout->addWidget(m_retryButton, 0, Qt::AlignCenter);
    m_loadingWidget->setupErrorWidget(errorLayout);
    connect(m_retryButton, &QPushButton::clicked, this, [this]() {
        m_loadingWidget->showLoading();
        QTimer::singleShot(100, this, &GalleryWidget::reload);
    });

    setControlsEnabled(false); // Disable controls until album is selected
}

void GalleryWidget::reload()
{
    m_loaded = false;
    load();
}

/*Load is called when the tab is active*/
void GalleryWidget::load()
{
    if (m_loaded)
        return;

    m_loaded = true;
    qDebug() << "Before reading DCIM directory";

    auto *watcher = new QFutureWatcher<AFCFileTree>(this);
    auto future = ServiceManager::getFileTreeAsync(m_device, "/DCIM", true);
    watcher->setFuture(future);

    connect(watcher, &QFutureWatcher<AFCFileTree>::finished, [this, watcher]() {
        watcher->deleteLater();
        loadAlbumList(watcher->result());
    });
}

void GalleryWidget::setupControlsLayout()
{
    m_controlsLayout = new QHBoxLayout();
    m_controlsLayout->setSpacing(5);
    m_controlsLayout->setContentsMargins(7, 7, 7, 7);

    m_importButton = new QPushButton("Import");

    // Sort order combo box
    QLabel *sortLabel = new QLabel("Sort:");
    sortLabel->setStyleSheet(mergeStyles(sortLabel, "font-weight: 600;"));
    m_sortComboBox = new QComboBox();
    m_sortComboBox->addItem("Newest First",
                            static_cast<int>(PhotoModel::NewestFirst));
    m_sortComboBox->addItem("Oldest First",
                            static_cast<int>(PhotoModel::OldestFirst));
    m_sortComboBox->setCurrentIndex(0);   // Default to Newest First
    m_sortComboBox->setMinimumWidth(100); // Ensure text fits
    m_sortComboBox->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);

    // Filter combo box
    QLabel *filterLabel = new QLabel("Filter:");
    filterLabel->setStyleSheet(mergeStyles(filterLabel, "font-weight: 600;"));
    m_filterComboBox = new QComboBox();
    m_filterComboBox->addItem("All Media", static_cast<int>(PhotoModel::All));
    m_filterComboBox->addItem("Images Only",
                              static_cast<int>(PhotoModel::ImagesOnly));
    m_filterComboBox->addItem("Videos Only",
                              static_cast<int>(PhotoModel::VideosOnly));
    m_filterComboBox->setCurrentIndex(
        static_cast<int>(PhotoModel::All)); // Default to All
    m_filterComboBox->setMinimumWidth(100); // Ensure text fits
    m_filterComboBox->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);

    // Export buttons
    m_exportSelectedButton = new QPushButton("Export Selected");
    m_exportSelectedButton->setEnabled(false);
    m_exportSelectedButton->setSizePolicy(QSizePolicy::Preferred,
                                          QSizePolicy::Fixed);
    m_exportAllButton = new QPushButton("Export All");
    m_exportAllButton->setEnabled(false);

    // Back button
    m_backButton = new ZIconWidget(
        QIcon(":/resources/icons/MaterialSymbolsArrowLeftAlt.png"),
        "Back to Albums");
    m_backButton->setMaximumWidth(30);
    m_backButton->hide(); // Hidden initially

    // Connect signals
    connect(m_sortComboBox, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &GalleryWidget::onSortOrderChanged);
    connect(m_filterComboBox,
            QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            &GalleryWidget::onFilterChanged);
    connect(m_exportSelectedButton, &QPushButton::clicked, this,
            &GalleryWidget::onExportSelected);
    connect(m_exportAllButton, &QPushButton::clicked, this,
            &GalleryWidget::onExportAll);
    connect(m_backButton, &ZIconWidget::clicked, this,
            &GalleryWidget::onBackToAlbums);

    connect(m_importButton, &QPushButton::clicked, this,
            &GalleryWidget::handleImport);

    // Add widgets to layout
    m_controlsLayout->addWidget(m_backButton);
    m_controlsLayout->addWidget(m_importButton);
    m_controlsLayout->addWidget(sortLabel);
    m_controlsLayout->addWidget(m_sortComboBox);
    m_controlsLayout->addWidget(filterLabel);
    m_controlsLayout->addWidget(m_filterComboBox);
    m_controlsLayout->addStretch(); // Push export buttons to the right
    m_controlsLayout->addWidget(m_exportSelectedButton);
    m_controlsLayout->addWidget(m_exportAllButton);

    QWidget *controlsWidget = new QWidget();
    controlsWidget->setLayout(m_controlsLayout);
    controlsWidget->setObjectName("controlsWidget");
    controlsWidget->setStyleSheet("QWidget#controlsWidget { "
                                  "  padding: 2px; "
                                  "}");

    m_mainLayout->addWidget(controlsWidget);
}

void GalleryWidget::onSortOrderChanged()
{
    if (!m_model)
        return;

    int sortValue = m_sortComboBox->currentData().toInt();
    PhotoModel::SortOrder order = static_cast<PhotoModel::SortOrder>(sortValue);
    m_model->setSortOrder(order);

    qDebug() << "Sort order changed to:"
             << (order == PhotoModel::NewestFirst ? "Newest First"
                                                  : "Oldest First");
}

PhotoModel::FilterType GalleryWidget::getCurrentFilterType() const
{
    int filterValue = m_filterComboBox->currentData().toInt();
    return static_cast<PhotoModel::FilterType>(filterValue);
}

void GalleryWidget::onFilterChanged()
{
    if (!m_model)
        return;

    PhotoModel::FilterType filter = getCurrentFilterType();
    m_model->setFilterType(filter);

    QString filterName = m_filterComboBox->currentText();
    qDebug() << "Filter changed to:" << filterName;
}

void GalleryWidget::onExportSelected()
{
    // if we are exporting from album selection view
    if (m_loadingWidget->currentWidget() == m_albumSelectionWidget) {

        QModelIndexList selectedIndexes =
            m_albumListView->selectionModel()->selectedIndexes();
        // QStringList filePaths =
        // m_albumModel->getSelectedFilePaths(selectedIndexes);

        QStringList paths;
        for (const QModelIndex &index : selectedIndexes) {
            if (index.isValid() &&
                index.row() < m_albumListView->model()->rowCount()) {
                paths.append(index.data(Qt::UserRole).toString());
            } else {
                qDebug() << "Invalid index in selection:" << index;
            }
        }
        // /DCIM/100APPLE
        qDebug() << "Selected file paths:" << paths;

        auto *exportAlbum = new ExportAlbum(m_device, paths, this);
        exportAlbum->show();
        return;
    }

    if (!m_model || !m_listView->selectionModel()->hasSelection()) {
        QMessageBox::information(this, "No Selection",
                                 "Please select photos to export.");
        return;
    }

    QModelIndexList selectedIndexes =
        m_listView->selectionModel()->selectedIndexes();
    QStringList filePaths = m_model->getSelectedFilePaths(selectedIndexes);

    if (filePaths.isEmpty()) {
        QMessageBox::information(this, "No Items",
                                 "No valid items selected for export.");
        return;
    }

    QString exportDir = selectExportDirectory();
    if (exportDir.isEmpty()) {
        return;
    }

    QList<ExportItem> exportItems;
    for (const QString &filePath : filePaths) {
        QString fileName = filePath.split('/').last();
        exportItems.append(ExportItem(filePath, fileName, m_device->udid));
    }

    qDebug() << "Starting export of selected files:" << exportItems.size()
             << "items to" << exportDir;

    ExportManager::sharedInstance()->startExport(m_device, exportItems,
                                                 exportDir);
}

void GalleryWidget::onExportAll()
{
    // if we are exporting from album selection view
    if (m_loadingWidget->currentWidget() == m_albumSelectionWidget) {

        // gel all available albums
        QStringList paths;
        for (int row = 0; row < m_albumListView->model()->rowCount(); ++row) {
            QModelIndex index = m_albumListView->model()->index(row, 0);
            if (index.isValid()) {
                paths.append(index.data(Qt::UserRole).toString());
            }
        }

        auto *exportAlbum = new ExportAlbum(m_device, paths, this);
        exportAlbum->show();
        return;
    }

    if (!m_model)
        return;

    QStringList filePaths = m_model->getFilteredFilePaths();

    if (filePaths.isEmpty()) {
        QMessageBox::information(this, "No Items", "No items to export.");
        return;
    }

    QString message =
        QString("Export all %1 items currently shown?").arg(filePaths.size());
    int reply = QMessageBox::question(this, "Export All", message,
                                      QMessageBox::Yes | QMessageBox::No,
                                      QMessageBox::No);

    if (reply != QMessageBox::Yes) {
        return;
    }

    QString exportDir = selectExportDirectory();
    if (exportDir.isEmpty()) {
        return;
    }

    QList<ExportItem> exportItems;
    for (const QString &filePath : filePaths) {
        QString fileName = filePath.split('/').last();
        exportItems.append(ExportItem(filePath, fileName, m_device->udid));
    }

    qDebug() << "Starting export of:" << exportItems.size() << "items to"
             << exportDir;

    // Start export and the manager will show its own dialog
    ExportManager::sharedInstance()->startExport(m_device, exportItems,
                                                 exportDir);
}

QString GalleryWidget::selectExportDirectory()
{
    QString defaultDir =
        QStandardPaths::writableLocation(QStandardPaths::PicturesLocation);

    QString selectedDir = QFileDialog::getExistingDirectory(
        this, "Select Export Directory", defaultDir,
        QFileDialog::ShowDirsOnly | QFileDialog::DontResolveSymlinks);

    return selectedDir;
}

void GalleryWidget::setupAlbumSelectionView()
{
    m_albumSelectionWidget = new QWidget();
    QVBoxLayout *layout = new QVBoxLayout(m_albumSelectionWidget);
    layout->setContentsMargins(0, 0, 0, 0);
    // Add instructions label
    QLabel *instructionLabel = new QLabel("Select a photo album:");
    instructionLabel->setStyleSheet("font-weight: bold;");
    layout->addWidget(instructionLabel);

    m_albumListView = new QListView();
    m_albumListView->setViewMode(QListView::IconMode);
    m_albumListView->setFlow(QListView::LeftToRight);
    m_albumListView->setWrapping(true);
    m_albumListView->setResizeMode(QListView::Adjust);
    m_albumListView->setIconSize(QSize(120, 120));
    m_albumListView->setSpacing(10);
    m_albumListView->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_albumListView->setUniformItemSizes(true);

    m_albumListView->setStyleSheet("QListView { "
                                   "    border-top: 1px solid #c1c1c1ff; "
                                   "    background-color: transparent; "
                                   "    padding: 0px;"
                                   "} "
                                   "QListView::item { "
                                   "    width: 150px; "
                                   "    height: 150px; "
                                   "    margin: 2px; "
                                   "}");

    layout->addWidget(m_albumListView);

    m_loadingWidget->setupContentWidget(m_albumSelectionWidget);

    connect(m_albumListView, &QListView::doubleClicked, this,
            [this](const QModelIndex &index) {
                if (!index.isValid())
                    return;
                QString albumPath = index.data(Qt::UserRole).toString();
                onAlbumSelected(albumPath);
            });
}

void GalleryWidget::setupPhotoGalleryView()
{
    m_photoGalleryWidget = new QWidget();
    QVBoxLayout *layout = new QVBoxLayout(m_photoGalleryWidget);
    layout->setContentsMargins(0, 0, 0, 0);

    // Create list view for photos
    m_listView = new QListView();
    m_listView->setViewMode(QListView::IconMode);
    m_listView->setFlow(QListView::LeftToRight);
    m_listView->setWrapping(true);
    m_listView->setResizeMode(QListView::Adjust);
    m_listView->setIconSize(QSize(120, 120));
    m_listView->setSpacing(10);
    m_listView->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_listView->setUniformItemSizes(true);
    m_listView->setContextMenuPolicy(Qt::CustomContextMenu);

    m_listView->setStyleSheet("QListView { "
                              "    border-top: 1px solid #c1c1c1ff; "
                              "    background-color: transparent; "
                              "    padding: 0px;"
                              "} "
                              "QListView::item { "
                              "    width: 150px; "
                              "    height: 150px; "
                              "    margin: 2px; "
                              "}");

    layout->addWidget(m_listView);

    // Add the photo gallery widget to stacked widget
    m_loadingWidget->setupAditionalWidget(m_photoGalleryWidget);

    // Connect double-click to open preview dialog
    connect(m_listView, &QListView::doubleClicked, this,
            [this](const QModelIndex &index) {
                if (!index.isValid())
                    return;

                QString filePath =
                    m_model->data(index, Qt::UserRole).toString();
                if (filePath.isEmpty())
                    return;

                qDebug() << "Opening preview for" << filePath;
                auto *previewDialog = new MediaPreviewDialog(
                    m_device, m_device->afcClient, filePath, this);
                previewDialog->setAttribute(Qt::WA_DeleteOnClose);
                previewDialog->show();
            });

    connect(m_listView, &QListView::customContextMenuRequested, this,
            &GalleryWidget::onPhotoContextMenu);
}

void GalleryWidget::loadAlbumList(const AFCFileTree &dcimTree)
{
    if (!dcimTree.success) {
        qDebug() << "Failed to read DCIM directory";
        m_loadingWidget->showError();
        QMessageBox::warning(this, "Error",
                             "Could not access DCIM directory on device.");
        return;
    }

    qDebug() << "DCIM directory read successfully, found"
             << dcimTree.entries.size() << "entries";

    m_albumModel = new QStandardItemModel(this);

    for (const MediaEntry &entry : dcimTree.entries) {
        QString albumName = QString::fromStdString(entry.name);

        // Check if it's a directory and matches common iOS photo album patterns
        if (entry.isDir &&
            (albumName.contains("APPLE") ||
             QRegularExpression("^\\d{3}APPLE$").match(albumName).hasMatch() ||
             QRegularExpression("^\\d{4}\\d{2}\\d{2}$")
                 .match(albumName)
                 .hasMatch())) {
            auto *item = new QStandardItem(albumName);
            QString fullPath = QString("/DCIM/%1").arg(albumName);
            item->setData(fullPath, Qt::UserRole); // Store full path

            item->setIcon(QIcon::fromTheme("folder"));
            m_albumModel->appendRow(item);

            loadAlbumThumbnailAsync(fullPath, item);
        }
    }

    m_albumListView->setModel(m_albumModel);
    m_loadingWidget->stop();
    m_loadingWidget->switchToWidget(m_albumSelectionWidget);
    m_exportAllButton->setEnabled(m_albumModel->rowCount() > 0);

    connect(m_albumListView->selectionModel(),
            &QItemSelectionModel::selectionChanged, this, [this]() {
                bool hasSelection =
                    m_albumListView->selectionModel()->hasSelection();
                m_exportSelectedButton->setEnabled(hasSelection);
            });
}

void GalleryWidget::onAlbumSelected(const QString &albumPath)
{
    m_currentAlbumPath = albumPath;

    // Create model if not exists
    if (!m_model) {
        m_model = new PhotoModel(m_device, getCurrentFilterType(), this);
        m_listView->setModel(m_model);

        connect(m_model, &PhotoModel::albumPathSet, this, [this]() {
            // Switch to photo gallery view once album is loaded
            m_loadingWidget->switchToWidget(m_photoGalleryWidget);
            // Enable controls and show back button
            setControlsEnabled(true);
            m_backButton->show();
        });

        connect(m_model, &PhotoModel::timedOut, this, [this]() {
            m_loadingWidget->showError("Timed out loading album");
        });

        // Update export button states based on selection
        connect(m_listView->selectionModel(),
                &QItemSelectionModel::selectionChanged, this, [this]() {
                    bool hasSelection =
                        m_listView->selectionModel()->hasSelection();
                    m_exportSelectedButton->setEnabled(hasSelection);
                });
    }

    // Set album path and load photos
    m_model->setAlbumPath(albumPath);

    m_loadingWidget->showLoading();
}

void GalleryWidget::onBackToAlbums()
{
    // Switch back to album selection view
    m_loadingWidget->switchToWidget(m_albumSelectionWidget);

    if (m_model) {
        m_model->clear();
    }

    // Disable controls and hide back button
    setControlsEnabled(false);
    m_backButton->hide();
    // Clear current album path
    m_currentAlbumPath.clear();
}

void GalleryWidget::setControlsEnabled(bool enabled)
{
    m_sortComboBox->setEnabled(enabled);
    m_filterComboBox->setEnabled(enabled);
    m_exportSelectedButton->setEnabled(
        enabled && m_listView && m_listView->selectionModel()->hasSelection());
}

/*
    FIXME: this needs to be refactored once we
    figure out how to query Photos.sqlite
    Check out:
    https://github.com/ScottKjr3347/iOS_Local_PL_Photos.sqlite_Queries
*/
QIcon GalleryWidget::loadAlbumThumbnail(const QString &albumPath)
{
    // Get album directory contents
    AFCFileTree albumTree = ServiceManager::safeGetFileTree(
        m_device, albumPath.toStdString(), false);

    if (!albumTree.success) {
        qDebug() << "Failed to read album directory:" << albumPath;
        return QIcon();
    }

    // Find the first image file
    QString firstImagePath;
    for (const MediaEntry &entry : albumTree.entries) {
        QString fileName = QString::fromStdString(entry.name);

        if (!entry.isDir && (fileName.endsWith(".JPG", Qt::CaseInsensitive) ||
                             fileName.endsWith(".PNG", Qt::CaseInsensitive) ||
                             fileName.endsWith(".HEIC", Qt::CaseInsensitive))) {
            firstImagePath = albumPath + "/" + fileName;
            break;
        }
    }

    if (firstImagePath.isEmpty()) {
        qDebug() << "No images found in album:" << albumPath;
        return QIcon();
    }

    // Load the thumbnail using ServiceManager
    QByteArray imageData = ServiceManager::safeReadAfcFileToByteArray(
        m_device, firstImagePath.toUtf8().constData());

    if (imageData.isEmpty()) {
        qDebug() << "Could not read image data for thumbnail:"
                 << firstImagePath;
        return QIcon();
    }

    QPixmap thumbnail;

    if (firstImagePath.endsWith(".HEIC", Qt::CaseInsensitive)) {
        qDebug() << "Loading HEIC thumbnail from:" << firstImagePath;
        // FIXME: move to servicemanager
        thumbnail = load_heic(imageData);
    } else {
        // Load regular image formats
        if (!thumbnail.loadFromData(imageData)) {
            qDebug() << "Could not decode image data for thumbnail:"
                     << firstImagePath;
            return QIcon();
        }
    }

    if (thumbnail.isNull()) {
        qDebug() << "Failed to load thumbnail from:" << firstImagePath;
        return QIcon();
    }

    return QIcon(thumbnail);
}

void GalleryWidget::loadAlbumThumbnailAsync(const QString &albumPath,
                                            QStandardItem *item)
{
    // Create a future watcher to handle the async result
    auto *watcher = new QFutureWatcher<QIcon>(this);

    // Connect the finished signal to update the item icon
    connect(watcher, &QFutureWatcher<QIcon>::finished, this, [watcher, item]() {
        QIcon result = watcher->result();
        if (!result.isNull()) {
            item->setIcon(result);
        }
        // The item keeps the folder icon if thumbnail loading fails
        watcher->deleteLater();
    });

    // Start the async operation
    QFuture<QIcon> future = QtConcurrent::run(
        [this, albumPath]() { return loadAlbumThumbnail(albumPath); });

    watcher->setFuture(future);
}

void GalleryWidget::onPhotoContextMenu(const QPoint &pos)
{
    QModelIndex index = m_listView->indexAt(pos);
    if (!index.isValid()) {
        return;
    }

    // Make sure the item is selected
    if (!m_listView->selectionModel()->isSelected(index)) {
        m_listView->selectionModel()->select(
            index, QItemSelectionModel::ClearAndSelect);
    }

    QMenu contextMenu(this);
    QAction *previewAction = contextMenu.addAction("Preview");
    contextMenu.addSeparator();
    QAction *exportAction = contextMenu.addAction("Export");

    exportAction->setEnabled(m_listView->selectionModel()->hasSelection());

    connect(previewAction, &QAction::triggered, this, [this, index]() {
        // Re-use the double-click logic
        if (!index.isValid())
            return;

        QString filePath = m_model->data(index, Qt::UserRole).toString();
        if (filePath.isEmpty())
            return;

        qDebug() << "Opening preview for" << filePath;
        auto *previewDialog = new MediaPreviewDialog(
            m_device, m_device->afcClient, filePath, this);
        previewDialog->setAttribute(Qt::WA_DeleteOnClose);
        previewDialog->show();
    });

    connect(exportAction, &QAction::triggered, this,
            &GalleryWidget::onExportSelected);

    contextMenu.exec(m_listView->viewport()->mapToGlobal(pos));
}

void GalleryWidget::handleImport()
{
    QStringList filePaths = QFileDialog::getOpenFileNames(
        this, "Select Photos to Import",
        QStandardPaths::writableLocation(QStandardPaths::PicturesLocation),
        "Images (*.jpg *.jpeg *.png *.heic);;All Files (*)");

    if (filePaths.isEmpty()) {
        return;
    }

    qDebug() << "Selected files for import:" << filePaths;

    PhotoImportDialog dialog(filePaths, this);
    dialog.exec();
}

GalleryWidget::~GalleryWidget()
{
    qDebug() << "GalleryWidget destructor called";
}
