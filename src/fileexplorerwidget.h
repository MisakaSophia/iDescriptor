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

#ifndef FILEEXPLORERWIDGET_H
#define FILEEXPLORERWIDGET_H

#include "iDescriptor.h"
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
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
#include <QApplication>
#include <QDebug>
#include <QDesktopServices>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QIcon>
#include <QInputDialog>
#include <QMessageBox>
#include <QPainter>
#include <QPalette>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSplitter>
#include <QSplitterHandle>
#include <QTreeWidget>
#include <QVariant>
#include "zloadingwidget.h"


class FileExplorerWidget : public QWidget
{
    Q_OBJECT
public:
    explicit FileExplorerWidget(const iDescriptorDevice *device,
                                QWidget *parent = nullptr);
    void init();

private slots:
    void onSidebarItemClicked(QTreeWidgetItem *item, int column);

private:
    ZLoadingWidget *m_loadingWidget;
    QSplitter *m_mainSplitter;
    QStackedWidget *m_stackedWidget;
    AfcClientHandle *currentAfcClient;
    QTreeWidget *m_sidebarTree;
    const iDescriptorDevice *m_device;

    // Tree items
    QTreeWidgetItem *m_defaultAfcItem;
    QTreeWidgetItem *m_jailbrokenAfcItem;
    QTreeWidgetItem *m_favoritePlacesItem;

    void setupSidebar();
    void loadFavoritePlaces();
    void saveFavoritePlace(const QString &alias, const QString &path);
    void saveFavoritePlaceAfc2(const QString &alias, const QString &path);
    void onSidebarContextMenuRequested(const QPoint &pos);
    bool m_loaded = false;
};

#endif // FILEEXPLORERWIDGET_H
