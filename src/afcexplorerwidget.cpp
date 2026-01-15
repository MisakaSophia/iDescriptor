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

#include "afcexplorerwidget.h"
#include "exportmanager.h"
#include "iDescriptor-ui.h"
#include "iDescriptor.h"
#include "mediapreviewdialog.h"
#include "servicemanager.h"
#include "settingsmanager.h"
#include <QDebug>
#include <QDesktopServices>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QIcon>
#include <QInputDialog>
#include <QMenu>
#include <QMessageBox>
#include <QPushButton>
#include <QRegularExpression>
#include <QScrollBar>
#include <QSignalBlocker>
#include <QSplitter>
#include <QStackedWidget>
#include <QStyle>
#include <QTemporaryDir>
#include <QTreeWidget>
#include <QVariant>

AfcExplorerWidget::AfcExplorerWidget(iDescriptorDevice *device, bool favEnabled,
                                     AfcClientHandle *afcClient, QString root,
                                     QWidget *parent)
    : QWidget(parent), m_device(device), m_favEnabled(favEnabled),
      m_afc(afcClient), m_errorMessage("Failed to load directory"), m_root(root)
{
    // Setup file explorer
    setupFileExplorer();

    // Main layout
    QHBoxLayout *mainLayout = new QHBoxLayout(this);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    setLayout(mainLayout);

    mainLayout->addWidget(m_explorer);

    // Initialize
    m_history.push(m_root);
    m_currentHistoryIndex = 0;
    m_forwardHistory.clear();
    loadPath(m_root);

    setupContextMenu();
}

void AfcExplorerWidget::goBack()
{
    if (m_history.size() > 1) {
        // Move current path to forward history
        QString currentPath = m_history.pop();
        m_forwardHistory.push(currentPath);

        QString prevPath = m_history.top();
        loadPath(prevPath);
    }
}

void AfcExplorerWidget::goForward()
{
    if (!m_forwardHistory.isEmpty()) {
        QString forwardPath = m_forwardHistory.pop();
        m_history.push(forwardPath);
        loadPath(forwardPath);
    }
}

void AfcExplorerWidget::onItemDoubleClicked(QListWidgetItem *item)
{
    QVariant data = item->data(Qt::UserRole);
    bool isDir = data.toBool();
    QString name = item->text();

    // Use breadcrumb to get current path
    QString currPath = "/";
    if (!m_history.isEmpty())
        currPath = m_history.top();

    if (!currPath.endsWith("/"))
        currPath += "/";
    QString nextPath = currPath == "/" ? "/" + name : currPath + name;

    if (isDir) {
        // Clear forward history when navigating to a new directory
        m_forwardHistory.clear();
        m_history.push(nextPath);
        loadPath(nextPath);
    } else {
        const QString lowerFileName = name.toLower();
        const bool isPreviewable =
            lowerFileName.endsWith(".mp4") || lowerFileName.endsWith(".m4v") ||
            lowerFileName.endsWith(".mov") || lowerFileName.endsWith(".avi") ||
            lowerFileName.endsWith(".mkv") || lowerFileName.endsWith(".jpg") ||
            lowerFileName.endsWith(".jpeg") || lowerFileName.endsWith(".png") ||
            lowerFileName.endsWith(".gif") || lowerFileName.endsWith(".bmp");

        if (isPreviewable) {
            auto *previewDialog =
                new MediaPreviewDialog(m_device, m_afc, nextPath, this);
            previewDialog->setAttribute(Qt::WA_DeleteOnClose);
            previewDialog->show();
        } else {
            openWithDesktopService(nextPath, name);
        }
    }
}

void AfcExplorerWidget::openWithDesktopService(const QString &devicePath,
                                               const QString &fileName)
{
    QTemporaryDir *tempDir = new QTemporaryDir();
    if (!tempDir->isValid()) {
        QMessageBox::critical(this, "Error",
                              "Could not create a temporary directory.");
        delete tempDir;
        return;
    }

    QString localPath = tempDir->path() + "/" + fileName;
    int result = exportFileToPath(m_afc, devicePath.toUtf8().constData(),
                                  localPath.toUtf8().constData());

    if (result == 0) {
        QDesktopServices::openUrl(QUrl::fromLocalFile(localPath));
        // TODO: Clean up tempDir in destructor or keep a list of temp dirs
    } else {
        QMessageBox::warning(this, "Export Failed",
                             "Could not export the file from the device.");
        delete tempDir;
    }
}

void AfcExplorerWidget::onAddressBarReturnPressed()
{
    QString path = m_addressBar->text().trimmed();
    if (path.isEmpty()) {
        path = "/";
    }

    // Normalize the path
    if (!path.startsWith("/")) {
        path = "/" + path;
    }

    // Remove duplicate slashes
    path = path.replace(QRegularExpression("/+"), "/");

    // Clear forward history when navigating to a new path
    m_forwardHistory.clear();

    // Update history and load the path
    m_history.push(path);
    loadPath(path);
}

void AfcExplorerWidget::updateNavigationButtons()
{
    // Update button states based on history
    if (m_backButton) {
        m_backButton->setEnabled(m_history.size() > 1);
    }
    if (m_forwardButton) {
        m_forwardButton->setEnabled(!m_forwardHistory.isEmpty());
    }
    if (m_upButton) {
        bool canGoUp = !m_history.isEmpty() && m_history.top() != "/";
        m_upButton->setEnabled(canGoUp);
    }
}

void AfcExplorerWidget::updateAddressBar(const QString &path)
{
    // Update the address bar with the current path
    m_addressBar->setText(path);
}

void AfcExplorerWidget::loadPath(const QString &path)
{
    updateAddressBar(path);
    updateNavigationButtons();

    AFCFileTree tree = ServiceManager::safeGetFileTree(
        m_device, path.toStdString(), true, m_afc);
    if (!tree.success) {
        showErrorState();
        return;
    }

    // Clear the file list and show file list state
    m_fileList->clear();
    showFileListState();

    for (const auto &entry : tree.entries) {
        QListWidgetItem *item =
            new QListWidgetItem(QString::fromStdString(entry.name));
        item->setData(Qt::UserRole, entry.isDir);
        if (entry.isDir) {
            QIcon folderIcon = QIcon::fromTheme("folder");
            if (folderIcon.isNull()) {
                item->setIcon(
                    QIcon(":/resources/icons/MaterialSymbolsFolder.png"));
            } else {
                item->setIcon(folderIcon);
            }
        } else {
            QIcon fileIcon = QIcon::fromTheme("text-x-generic");
            if (fileIcon.isNull()) {
                item->setIcon(
                    QIcon(":/resources/icons/IcBaselineInsertDriveFile.png"));
            } else {
                item->setIcon(fileIcon);
            }
        }
        m_fileList->addItem(item);
    }
}

void AfcExplorerWidget::setupContextMenu()
{
    m_fileList->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_fileList, &QListWidget::customContextMenuRequested, this,
            &AfcExplorerWidget::onFileListContextMenu);
}

void AfcExplorerWidget::onFileListContextMenu(const QPoint &pos)
{
    QListWidgetItem *item = m_fileList->itemAt(pos);
    if (!item)
        return;

    bool isDir = item->data(Qt::UserRole).toBool();
    if (isDir)
        return; // TODO: Implement directory export later - Only export files
                // for now

    QMenu menu;
    QAction *exportAction = menu.addAction("Export");
    QAction *openAction = menu.addAction("Open");
    QAction *openNativeAction = menu.addAction("Open Externally");
    QAction *selectedAction =
        menu.exec(m_fileList->viewport()->mapToGlobal(pos));
    if (selectedAction == exportAction) {
        QList<QListWidgetItem *> selectedItems = m_fileList->selectedItems();
        QList<QListWidgetItem *> filesToExport;
        if (selectedItems.isEmpty())
            filesToExport.append(item); // fallback: just the clicked one
        else {
            for (QListWidgetItem *selItem : selectedItems) {
                if (!selItem->data(Qt::UserRole).toBool())
                    filesToExport.append(selItem);
            }
        }
        if (filesToExport.isEmpty())
            return;

        QString dir =
            QFileDialog::getExistingDirectory(this, "Select Export Directory");
        if (dir.isEmpty())
            return;

        // Convert to ExportItem list
        QList<ExportItem> exportItems;
        QString currPath = "/";
        if (!m_history.isEmpty())
            currPath = m_history.top();
        if (!currPath.endsWith("/"))
            currPath += "/";

        // FIXME: index
        int index = 0;
        for (QListWidgetItem *selItem : filesToExport) {
            QString fileName = selItem->text();
            QString devicePath =
                currPath == "/" ? "/" + fileName : currPath + fileName;
            exportItems.append(ExportItem(devicePath, fileName, index));
            index++;
        }

        // Start export with singleton - manager will show its own dialog
        ExportManager::sharedInstance()->startExport(m_device, exportItems, dir,
                                                     m_afc);
    } else if (selectedAction == openAction) {
        onItemDoubleClicked(item);
    } else if (selectedAction == openNativeAction) {
        QString fileName = item->text();
        QString currPath = "/";
        if (!m_history.isEmpty())
            currPath = m_history.top();
        if (!currPath.endsWith("/"))
            currPath += "/";
        QString devicePath =
            currPath == "/" ? "/" + fileName : currPath + fileName;

        openWithDesktopService(devicePath, fileName);
    }
}

void AfcExplorerWidget::onExportClicked()
{
    QList<QListWidgetItem *> selectedItems = m_fileList->selectedItems();
    if (selectedItems.isEmpty())
        return;

    // Only files (not directories) - TODO: Implement directory export later
    QList<QListWidgetItem *> filesToExport;
    for (QListWidgetItem *item : selectedItems) {
        if (!item->data(Qt::UserRole).toBool())
            filesToExport.append(item);
    }
    if (filesToExport.isEmpty())
        return;

    // Ask user for a directory to save all files
    QString dir =
        QFileDialog::getExistingDirectory(this, "Select Export Directory");
    if (dir.isEmpty())
        return;

    // Convert to ExportItem list
    QList<ExportItem> exportItems;
    QString currPath = "/";
    if (!m_history.isEmpty())
        currPath = m_history.top();
    if (!currPath.endsWith("/"))
        currPath += "/";

    int index = 0;
    for (QListWidgetItem *item : filesToExport) {
        QString fileName = item->text();
        QString devicePath =
            currPath == "/" ? "/" + fileName : currPath + fileName;
        exportItems.append(ExportItem(devicePath, fileName, index));
        index++;
    }

    // Start export with singleton - manager will show its own dialog
    ExportManager::sharedInstance()->startExport(m_device, exportItems, dir,
                                                 m_afc);
}

void AfcExplorerWidget::exportSelectedFile(QListWidgetItem *item,
                                           const QString &directory)
{
    QString fileName = item->text();
    QString currPath = "/";
    if (!m_history.isEmpty())
        currPath = m_history.top();
    if (!currPath.endsWith("/"))
        currPath += "/";
    QString devicePath = currPath == "/" ? "/" + fileName : currPath + fileName;
    qDebug() << "Exporting file:" << devicePath;

    // Save to selected directory
    QString savePath = directory + "/" + fileName;

    // FIXME: this should be async
    int result = exportFileToPath(m_afc, devicePath.toStdString().c_str(),
                                  savePath.toStdString().c_str());

    qDebug() << "Export result:" << result;

    if (result == 0) {
        qDebug() << "Exported" << devicePath << "to" << savePath;

        QMessageBox::StandardButton reply;
        reply = QMessageBox::question(
            this, "Export Successful",
            "File exported successfully. Would you like to see the directory?",
            QMessageBox::Yes | QMessageBox::No);
        if (reply == QMessageBox::Yes) {
            QDesktopServices::openUrl(QUrl::fromLocalFile(directory));
        }
    } else {
        qDebug() << "Failed to export" << devicePath;
        QMessageBox::warning(this, "Export Failed",
                             "Failed to export the file from the device");
    }
}

/*
    FIXME : abstract to services
    even though we are using safe wrappers,
    we better move this to services
*/
// FIXME: this should be async
// use connect to signals/slots to notify progress
// create a progress dialog to show progress
// dont do this on the main thread
int AfcExplorerWidget::exportFileToPath(AfcClientHandle *afc,
                                        const char *device_path,
                                        const char *local_path)
{
    AfcFileHandle *afcHandle = nullptr;
    IdeviceFfiError *err_open = ServiceManager::safeAfcFileOpen(
        m_device, device_path, AfcRdOnly, &afcHandle);
    if (err_open != nullptr) {
        qDebug() << "Failed to open file on device:" << device_path
                 << "Error Code:" << err_open->code
                 << "Message:" << err_open->message;
        idevice_error_free(err_open);
        return -1;
    }

    FILE *out = fopen(local_path, "wb");
    if (!out) {
        qDebug() << "Failed to open local file:" << local_path;
        IdeviceFfiError *err_close =
            ServiceManager::safeAfcFileClose(m_device, afcHandle);
        if (err_close != nullptr) {
            idevice_error_free(err_close);
        }
        return -1;
    }

    const size_t CHUNK_SIZE = 256 * 1024; // 256KB chunks
    uint8_t *chunkData = nullptr;
    size_t bytesRead = 0;

    // Read file in chunks
    while (true) {
        IdeviceFfiError *read_err = ServiceManager::safeAfcFileRead(
            m_device, afcHandle, &chunkData, CHUNK_SIZE, &bytesRead);

        if (read_err != nullptr) {
            qDebug() << "Error reading file:" << read_err->message;
            idevice_error_free(read_err);
            break;
        }

        if (bytesRead == 0) {
            // End of file reached
            break;
        }

        // Write chunk to local file
        size_t written = fwrite(chunkData, 1, bytesRead, out);

        // Free the memory allocated by afc_file_read
        afc_file_read_data_free(chunkData, bytesRead);
        chunkData = nullptr;

        if (written != bytesRead) {
            qDebug() << "Failed to write all bytes to local file";
            fclose(out);
            ServiceManager::safeAfcFileClose(m_device, afcHandle);
            return -1;
        }
    }

    fclose(out);

    IdeviceFfiError *err_close =
        ServiceManager::safeAfcFileClose(m_device, afcHandle);
    if (err_close != nullptr) {
        qDebug() << "Failed to close AFC file:" << err_close->message;
        idevice_error_free(err_close);
        return -1;
    }

    return 0;
}

// should be disabled if there is an error loading afc
void AfcExplorerWidget::onImportClicked()
{
    QStringList fileNames = QFileDialog::getOpenFileNames(this, "Import Files");
    if (fileNames.isEmpty())
        return;

    QString currPath = "/";
    if (!m_history.isEmpty())
        currPath = m_history.top();
    if (!currPath.endsWith("/"))
        currPath += "/";

    // Import each file
    for (const QString &localPath : fileNames) {
        QFileInfo fi(localPath);
        QString devicePath = currPath + fi.fileName();
        int result = importFileToDevice(m_afc, devicePath.toStdString().c_str(),
                                        localPath.toStdString().c_str());
        if (result == 0)
            qDebug() << "Imported" << localPath << "to" << devicePath;
        else
            qDebug() << "Failed to import" << localPath;
    }

    // Refresh file list
    loadPath(currPath);
}

/*
    FIXME : move to services
*/
int AfcExplorerWidget::importFileToDevice(AfcClientHandle *afc,
                                          const char *device_path,
                                          const char *local_path)
{
    QFile in(local_path);
    // if (!in.open(QIODevice::ReadOnly)) {
    //     qDebug() << "Failed to open local file for import:" << local_path;
    //     return -1;
    // }

    // uint64_t handle = 0;
    // if (ServiceManager::safeAfcFileOpen(m_device, device_path,
    // AFC_FOPEN_WRONLY,
    //                                     &handle, m_afc) != AFC_E_SUCCESS) {
    //     qDebug() << "Failed to open file on device for writing:" <<
    //     device_path; return -1;
    // }

    // char buffer[4096];
    // qint64 bytesRead;
    // while ((bytesRead = in.read(buffer, sizeof(buffer))) > 0) {
    //     uint32_t bytesWritten = 0;
    //     if (ServiceManager::safeAfcFileWrite(
    //             m_device, handle, buffer, static_cast<uint32_t>(bytesRead),
    //             &bytesWritten, m_afc) != AFC_E_SUCCESS ||
    //         bytesWritten != bytesRead) {
    //         qDebug() << "Failed to write to device file:" << device_path;
    //         ServiceManager::safeAfcFileClose(m_device, handle, m_afc);
    //         in.close();
    //         return -1;
    //     }
    // }

    // ServiceManager::safeAfcFileClose(m_device, handle, m_afc);
    in.close();
    return 0;
}

void AfcExplorerWidget::setupFileExplorer()
{
    m_explorer = new QWidget();
    QVBoxLayout *explorerLayout = new QVBoxLayout(m_explorer);
    explorerLayout->setContentsMargins(0, 0, 0, 0);
    m_explorer->setStyleSheet("border : none;");

    // Export/Import buttons layout
    m_exportBtn =
        new ZIconWidget(QIcon(":/resources/icons/PhExport.png"), "Export");
    m_importBtn = new ZIconWidget(
        QIcon(":/resources/icons/LetsIconsImport.png"), "Import");
    if (m_favEnabled) {
        m_addToFavoritesBtn = new ZIconWidget(
            QIcon(":/resources/icons/MaterialSymbolsFavorite.png"),
            "Add to Favorites");
    }

    // Navigation layout (Address Bar with embedded icons)
    m_navWidget = new QWidget();
    m_navWidget->setObjectName("navWidget");
    m_navWidget->setFocusPolicy(Qt::StrongFocus); // Make it focusable
    connect(qApp, &QApplication::paletteChanged, this,
            &AfcExplorerWidget::updateNavStyles);

    m_navWidget->setMaximumWidth(500);
    m_navWidget->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);

    QHBoxLayout *navContainerLayout = new QHBoxLayout();
    navContainerLayout->addStretch();
    navContainerLayout->addWidget(m_navWidget);
    navContainerLayout->addStretch();

    QHBoxLayout *navLayout = new QHBoxLayout(m_navWidget);
    navLayout->setContentsMargins(0, 0, 0, 0);
    navLayout->setSpacing(0);

    // Create navigation buttons using ClickableIconWidget
    QWidget *explorerLeftSideNavButtons = new QWidget();
    QHBoxLayout *leftNavLayout = new QHBoxLayout(explorerLeftSideNavButtons);
    // explorerLeftSideNavButtons->setStyleSheet("border-right: 1px solid
    // red;");
    leftNavLayout->setContentsMargins(0, 0, 0, 0);
    leftNavLayout->setSpacing(1);
    // rename to ziconwidget
    m_backButton = new ZIconWidget(
        QIcon(":/resources/icons/MaterialSymbolsArrowLeftAlt.png"), "Go Back");
    m_backButton->setEnabled(false);

    m_forwardButton = new ZIconWidget(
        QIcon(":/resources/icons/MaterialSymbolsArrowRightAlt.png"),
        "Go Forward");
    m_forwardButton->setEnabled(false);

    m_homeButton = new ZIconWidget(
        QIcon(":/resources/icons/MaterialSymbolsLightHome.png"), "Go Home");

    m_upButton = new ZIconWidget(
        QIcon(":/resources/icons/MaterialSymbolsArrowUpwardAltRounded.png"),
        "Go Up");
    m_upButton->setEnabled(false);

    m_enterButton = new ZIconWidget(
        QIcon(":/resources/icons/MaterialSymbolsLightKeyboardReturn.png"),
        "Navigate to path");

    m_addressBar = new QLineEdit();
    m_addressBar->setPlaceholderText("Enter path...");
    m_addressBar->setText("/");

    // Add widgets to navigation layout
    leftNavLayout->addWidget(m_backButton);
    leftNavLayout->addWidget(m_forwardButton);
    leftNavLayout->addWidget(m_homeButton);
    leftNavLayout->addWidget(m_upButton);
    navLayout->addWidget(explorerLeftSideNavButtons);
    navLayout->addWidget(m_addressBar);
    navLayout->addWidget(m_importBtn);
    navLayout->addWidget(m_exportBtn);
    if (m_favEnabled)
        navLayout->addWidget(m_addToFavoritesBtn);

    navLayout->addWidget(m_enterButton);

    // Add the container layout (which centers navWidget) to the main layout
    explorerLayout->addLayout(navContainerLayout);

    // Create stacked widget for content (file list or error state)
    m_contentStack = new QStackedWidget();

    // Create file list widget
    m_fileListWidget = new QWidget();
    QVBoxLayout *fileListLayout = new QVBoxLayout(m_fileListWidget);
    fileListLayout->setContentsMargins(0, 0, 0, 0);

    // File list
    m_fileList = new QListWidget();
    m_fileList->setSelectionMode(QAbstractItemView::ExtendedSelection);

    QScrollBar *vBar = m_fileList->QAbstractScrollArea::verticalScrollBar();
    vBar->setStyleSheet(styleSheet());
    fileListLayout->addWidget(m_fileList);

    // Create error widget
    m_errorWidget = new QWidget();
    QVBoxLayout *errorLayout = new QVBoxLayout(m_errorWidget);
    errorLayout->setContentsMargins(20, 20, 20, 20);

    errorLayout->addStretch();

    m_errorLabel = new QLabel(m_errorMessage);
    m_errorLabel->setAlignment(Qt::AlignCenter);
    m_errorLabel->setWordWrap(true);
    errorLayout->addWidget(m_errorLabel);

    m_retryButton = new QPushButton("Try Again");
    m_retryButton->setMaximumWidth(120);
    QHBoxLayout *buttonLayout = new QHBoxLayout();
    buttonLayout->addStretch();
    buttonLayout->addWidget(m_retryButton);
    buttonLayout->addStretch();
    errorLayout->addLayout(buttonLayout);

    errorLayout->addStretch();

    // Add both widgets to the stacked widget
    m_contentStack->addWidget(m_fileListWidget);
    m_contentStack->addWidget(m_errorWidget);

    // Start with file list view
    m_contentStack->setCurrentWidget(m_fileListWidget);

    explorerLayout->addWidget(m_contentStack);

    // Connect buttons and actions
    connect(m_backButton, &ZIconWidget::clicked, this,
            &AfcExplorerWidget::goBack);
    connect(m_forwardButton, &ZIconWidget::clicked, this,
            &AfcExplorerWidget::goForward);
    connect(m_homeButton, &ZIconWidget::clicked, this,
            &AfcExplorerWidget::goHome);
    connect(m_upButton, &ZIconWidget::clicked, this, &AfcExplorerWidget::goUp);
    connect(m_enterButton, &ZIconWidget::clicked, this,
            &AfcExplorerWidget::onAddressBarReturnPressed);
    connect(m_addressBar, &QLineEdit::returnPressed, this,
            &AfcExplorerWidget::onAddressBarReturnPressed);
    connect(m_fileList, &QListWidget::itemDoubleClicked, this,
            &AfcExplorerWidget::onItemDoubleClicked);
    connect(m_exportBtn, &ZIconWidget::clicked, this,
            &AfcExplorerWidget::onExportClicked);
    connect(m_importBtn, &ZIconWidget::clicked, this,
            &AfcExplorerWidget::onImportClicked);
    connect(m_retryButton, &QPushButton::clicked, this,
            &AfcExplorerWidget::onRetryClicked);
    connect(m_fileList->selectionModel(),
            &QItemSelectionModel::selectionChanged, this,
            &AfcExplorerWidget::updateButtonStates);

    if (m_favEnabled) {
        connect(m_addToFavoritesBtn, &ZIconWidget::clicked, this,
                &AfcExplorerWidget::onAddToFavoritesClicked);
    }

    updateNavigationButtons();
    updateButtonStates(); // Initialize button states
    updateNavStyles();
}

void AfcExplorerWidget::onAddToFavoritesClicked()
{
    QString currentPath = "/";
    if (!m_history.isEmpty())
        currentPath = m_history.top();

    bool ok;
    QString alias = QInputDialog::getText(
        this, "Add to Favorites",
        "Enter alias for this location:", QLineEdit::Normal, "Alias here", &ok);
    if (ok && !alias.isEmpty()) {
        emit favoritePlaceAdded(alias, currentPath);
    } else if (ok && alias.isEmpty()) {
        QMessageBox::warning(nullptr, "Invalid Input", "Alias was empty.");
        qWarning() << "Cannot save favorite place with empty alias";
    } else if (!ok) {
        qWarning() << "Failed to get alias for favorite place";
    }
}

void AfcExplorerWidget::updateNavStyles()
{
    bool isDark = isDarkMode();
    QColor lightColor = qApp->palette().color(QPalette::Light);
    QColor darkColor = qApp->palette().color(QPalette::Dark);
    QColor bgColor = isDark ? lightColor : darkColor;
    QColor borderColor = qApp->palette().color(QPalette::Mid);
    QColor accentColor = qApp->palette().color(QPalette::Highlight);

    QString navStyles = QString("QWidget#navWidget {"
                                "    background-color: %1;"
                                "    border: 1px solid %2;"
                                "    border-radius: 10px;"
                                "}"
                                "QWidget#navWidget {"
                                "    outline: 1px solid %3;"
                                "    outline-offset: 1px;"
                                "}")
                            .arg(bgColor.name())
                            .arg(bgColor.lighter().name())
                            .arg(accentColor.name());

    m_navWidget->setStyleSheet(navStyles);

    // Update address bar styles to complement the nav widget
    QString addressBarStyles =
        QString("QLineEdit { background-color: %1; border-radius: 10px; "
                "border: 1px solid %2; padding: 2px 4px; color: %3; }"
                "QLineEdit:focus {border: 3px solid %4; }")
            .arg(isDark ? QColor(Qt::white).name() : QColor(Qt::black).name())
            .arg(borderColor.lighter().name())
            .arg(isDark ? QColor(Qt::black).name() : QColor(Qt::white).name())
            .arg(COLOR_ACCENT_BLUE.name());

    m_addressBar->setStyleSheet(addressBarStyles);
}

void AfcExplorerWidget::updateButtonStates()
{
    QList<QListWidgetItem *> selectedItems = m_fileList->selectedItems();

    // Export is only enabled if non-directory items are selected
    bool hasExportableFiles = false;
    for (QListWidgetItem *item : selectedItems) {
        if (!item->data(Qt::UserRole).toBool()) { // Not a directory
            hasExportableFiles = true;
            break;
        }
    }
    m_exportBtn->setEnabled(hasExportableFiles);
}

void AfcExplorerWidget::setErrorMessage(const QString &message)
{
    m_errorMessage = message;
    if (m_errorLabel) {
        m_errorLabel->setText(m_errorMessage);
    }
}

void AfcExplorerWidget::showErrorState()
{
    if (m_contentStack) {
        m_contentStack->setCurrentWidget(m_errorWidget);
    }
}

void AfcExplorerWidget::showFileListState()
{
    if (m_contentStack) {
        m_contentStack->setCurrentWidget(m_fileListWidget);
    }
}

void AfcExplorerWidget::onRetryClicked()
{
    QString currentPath = "/";
    if (!m_history.isEmpty()) {
        currentPath = m_history.top();
    }
    loadPath(currentPath);
}

void AfcExplorerWidget::navigateToPath(const QString &path)
{
    if (path.isEmpty()) {
        return;
    }

    QString normalizedPath = path;
    if (!normalizedPath.startsWith("/")) {
        normalizedPath = "/" + normalizedPath;
    }
    normalizedPath = normalizedPath.replace(QRegularExpression("/+"), "/");

    m_history.push(normalizedPath);
    loadPath(normalizedPath);
}

void AfcExplorerWidget::goHome()
{
    // Clear forward history when navigating to a new directory
    m_forwardHistory.clear();
    m_history.push(m_root);
    loadPath(m_root);
}

void AfcExplorerWidget::goUp()
{
    if (m_history.isEmpty()) {
        return;
    }

    QString currentPath = m_history.top();

    // Can't go up from the root directory
    if (currentPath == "/") {
        return;
    }

    // Find the parent directory
    int lastSlashIndex = currentPath.lastIndexOf('/');
    QString parentPath =
        (lastSlashIndex > 0) ? currentPath.left(lastSlashIndex) : "/";

    // Going up is a new navigation action, so clear forward history
    m_forwardHistory.clear();

    // Add the new path to history and load it
    m_history.push(parentPath);
    loadPath(parentPath);
}
