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

#ifndef AFCEXPLORER_H
#define AFCEXPLORER_H

#include "iDescriptor-ui.h"
#include "iDescriptor.h"
#include <QAction>
#include <QEvent>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QPushButton>
#include <QSplitter>
#include <QStack>
#include <QStackedWidget>
#include <QString>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <QWidget>

class ExportManager;

class AfcExplorerWidget : public QWidget
{
    Q_OBJECT
public:
    explicit AfcExplorerWidget(const iDescriptorDevice *device = nullptr,
                               bool favEnabled = false,
                               AfcClientHandle *afcClient = nullptr,
                               QString root = "/", QWidget *parent = nullptr);
    void navigateToPath(const QString &path);
    void goHome();
signals:
    void favoritePlaceAdded(const QString &alias, const QString &path);

private slots:
    void goBack();
    void goForward();
    void onItemDoubleClicked(QListWidgetItem *item);
    void onAddressBarReturnPressed();
    void onFileListContextMenu(const QPoint &pos);
    void onExportClicked();
    void onImportClicked();
    void onAddToFavoritesClicked();
    void onRetryClicked();

private:
    QWidget *m_explorer;
    QWidget *m_navWidget;
    QStackedWidget *m_contentStack;
    QWidget *m_fileListWidget;
    QWidget *m_errorWidget;
    QLabel *m_errorLabel;
    QPushButton *m_retryButton;
    ZIconWidget *m_exportBtn;
    ZIconWidget *m_importBtn;
    ZIconWidget *m_addToFavoritesBtn;
    QListWidget *m_fileList;
    QStack<QString> m_history;
    QStack<QString> m_forwardHistory;
    int m_currentHistoryIndex;
    QLineEdit *m_addressBar;
    ZIconWidget *m_backButton;
    ZIconWidget *m_forwardButton;
    ZIconWidget *m_homeButton;
    ZIconWidget *m_upButton;
    ZIconWidget *m_enterButton;
    ZIconWidget *m_deleteButton;
    const iDescriptorDevice *m_device;
    bool m_favEnabled;
    AfcClientHandle *m_afc;
    QString m_errorMessage;
    QString m_root;

    // Export system
    ExportManager *m_exportManager;

    void setupFileExplorer();
    void loadPath(const QString &path);
    void updateAddressBar(const QString &path);
    void updateNavigationButtons();
    void setErrorMessage(const QString &message);
    void showErrorState();
    void showFileListState();
    void saveFavoritePlace(const QString &path, const QString &alias);
    void openWithDesktopService(QListWidgetItem *item);
    void onDeleteClicked();

    void setupContextMenu();
    void exportAndOpenSelectedFile(QListWidgetItem *item,
                                   const QString &directory);
    void updateButtonStates();
    void goUp();
#ifndef WIN32
    void updateNavStyles();

protected:
    void changeEvent(QEvent *event) override
    {
        if (event->type() == QEvent::PaletteChange) {
            updateNavStyles();
        }
        QWidget::changeEvent(event);
    }
#endif
};
#endif // AFCEXPLORER_H
