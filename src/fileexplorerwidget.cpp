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

#include "fileexplorerwidget.h"
#include "afcexplorerwidget.h"
#include "iDescriptor-ui.h"
#include "iDescriptor.h"
#include "mediapreviewdialog.h"
#include "settingsmanager.h"

FileExplorerWidget::FileExplorerWidget(const iDescriptorDevice *device,
                                       QWidget *parent)
    : QWidget(parent), m_device(device)
{
    QVBoxLayout *rootLayout = new QVBoxLayout(this);
    rootLayout->setContentsMargins(0, 0, 0, 0);

    m_loadingWidget = new ZLoadingWidget(true, this);
    rootLayout->addWidget(m_loadingWidget);
}

void FileExplorerWidget::init() {
    if (m_loaded) {
        qDebug() << "[FileExplorerWidget]: Already initialized, skipping init()";
        return;
    }
    m_loaded = true;
    m_mainSplitter = new ModernSplitter(Qt::Horizontal);

    // Main layout
    QWidget *contentContainer = new QWidget(this);
    QHBoxLayout *mainLayout = new QHBoxLayout(contentContainer);
    mainLayout->addWidget(m_mainSplitter);
    mainLayout->setContentsMargins(0, 0, 0, 0);

    m_loadingWidget->setupContentWidget(contentContainer);

    setupSidebar();

    // Create stacked widget with AFC explorers
    m_stackedWidget = new QStackedWidget();

    // Add normal AFC explorer (index 0)
    AfcExplorerWidget *afcExplorer =
        new AfcExplorerWidget(m_device, true, m_device->afcClient, "/", this);
    connect(afcExplorer, &AfcExplorerWidget::favoritePlaceAdded, this,
            &FileExplorerWidget::saveFavoritePlace);

    m_stackedWidget->addWidget(afcExplorer);

    // Add AFC2 explorer (index 1)
    AfcExplorerWidget *afc2Explorer =
        new AfcExplorerWidget(m_device, true, m_device->afc2Client, "/", this);
    connect(afc2Explorer, &AfcExplorerWidget::favoritePlaceAdded, this,
            &FileExplorerWidget::saveFavoritePlaceAfc2);
    m_stackedWidget->addWidget(afc2Explorer);

    // Start with normal AFC client
    m_stackedWidget->setCurrentIndex(0);

    // Add widgets to splitter
    m_mainSplitter->addWidget(m_sidebarTree);
    m_mainSplitter->addWidget(m_stackedWidget);
    m_mainSplitter->setSizes({400, 800});
    setLayout(mainLayout);

    connect(SettingsManager::sharedInstance(),
            &SettingsManager::favoritePlacesChanged, this,
            &FileExplorerWidget::loadFavoritePlaces);

    m_loadingWidget->stop(true);
}

void FileExplorerWidget::setupSidebar()
{
    m_sidebarTree = new QTreeWidget();
    m_sidebarTree->setHeaderLabel("Files");
    m_sidebarTree->setMinimumWidth(50);
    m_sidebarTree->setMaximumWidth(250);
    m_sidebarTree->setContextMenuPolicy(Qt::CustomContextMenu);

    QTreeWidgetItem *explorersRoot = new QTreeWidgetItem(m_sidebarTree);
    explorersRoot->setText(0, "Explorer");
    explorersRoot->setIcon(0, QIcon::fromTheme("folder"));
    explorersRoot->setExpanded(true);

    m_defaultAfcItem = new QTreeWidgetItem(explorersRoot);
    m_defaultAfcItem->setText(0, "Default");
    m_defaultAfcItem->setIcon(0, QIcon::fromTheme("folder"));

    m_jailbrokenAfcItem = new QTreeWidgetItem(explorersRoot);
    m_jailbrokenAfcItem->setText(0, "Jailbroken (AFC2)");
    m_jailbrokenAfcItem->setIcon(0, QIcon::fromTheme("folder"));

    // Common Places section
    QTreeWidgetItem *commonPlacesItem = new QTreeWidgetItem(m_sidebarTree);
    commonPlacesItem->setText(0, "Common Places");
    commonPlacesItem->setIcon(0, QIcon::fromTheme("places-bookmarks"));
    commonPlacesItem->setExpanded(true);

    QTreeWidgetItem *wallpapersItem = new QTreeWidgetItem(commonPlacesItem);
    QVariantMap dataMap;
    dataMap["path"] = "/DCIM";
    dataMap["alias"] = "Pictures";
    dataMap["afc2"] = false;
    wallpapersItem->setText(0, "Pictures");
    wallpapersItem->setIcon(0, QIcon::fromTheme("image-x-generic"));
    wallpapersItem->setData(0, Qt::UserRole, QVariant::fromValue(dataMap));

    // Favorite Places section
    m_favoritePlacesItem = new QTreeWidgetItem(m_sidebarTree);
    m_favoritePlacesItem->setText(0, "Favorite Places");
    m_favoritePlacesItem->setIcon(0, QIcon::fromTheme("user-bookmarks"));
    m_favoritePlacesItem->setExpanded(true);

    loadFavoritePlaces();

    connect(m_sidebarTree, &QTreeWidget::itemClicked, this,
            &FileExplorerWidget::onSidebarItemClicked);
    connect(m_sidebarTree, &QTreeWidget::customContextMenuRequested, this,
            &FileExplorerWidget::onSidebarContextMenuRequested);
}

void FileExplorerWidget::loadFavoritePlaces()
{
    m_favoritePlacesItem->takeChildren();
    SettingsManager *settings = SettingsManager::sharedInstance();
    QList<QPair<QString, QString>> favorites =
        settings->getFavoritePlaces("favorite_places/");

    for (const auto &favorite : favorites) {
        QString path = favorite.first;
        QString alias = favorite.second;
        QVariantMap dataMap;
        dataMap["path"] = path;
        dataMap["alias"] = alias;
        dataMap["afc2"] = false;

        QTreeWidgetItem *favoriteItem =
            new QTreeWidgetItem(m_favoritePlacesItem);
        favoriteItem->setText(0, alias);
        favoriteItem->setIcon(0, QIcon::fromTheme("folder-favorites"));
        favoriteItem->setData(0, Qt::UserRole, QVariant::fromValue(dataMap));
    }

    QList<QPair<QString, QString>> favorites_afc2 =
        settings->getFavoritePlaces("favorite_places_afc2/");

    for (const auto &favorite : favorites_afc2) {
        QString path = favorite.first;
        QString alias = favorite.second;
        QVariantMap dataMap;
        dataMap["path"] = path;
        dataMap["alias"] = alias;
        dataMap["afc2"] = true;
        QTreeWidgetItem *favoriteItem =
            new QTreeWidgetItem(m_favoritePlacesItem);
        favoriteItem->setText(0, alias);
        favoriteItem->setIcon(0, QIcon::fromTheme("folder-favorites"));
        favoriteItem->setData(0, Qt::UserRole, QVariant::fromValue(dataMap));
    }
}

void FileExplorerWidget::onSidebarItemClicked(QTreeWidgetItem *item, int column)
{
    Q_UNUSED(column);

    if (item == m_defaultAfcItem) {
        static_cast<AfcExplorerWidget *>(m_stackedWidget->widget(0))->goHome();
        m_stackedWidget->setCurrentIndex(0);
    } else if (item == m_jailbrokenAfcItem) {
        static_cast<AfcExplorerWidget *>(m_stackedWidget->widget(1))->goHome();
        m_stackedWidget->setCurrentIndex(1);
    }

    QVariant data = item->data(0, Qt::UserRole);
    if (data.isValid()) {
        QVariantMap dataMap = data.toMap();
        QString path = dataMap.value("path").toString();
        bool afc2 = dataMap.value("afc2").toBool();
        if (afc2) {
            m_stackedWidget->setCurrentIndex(1);
        } else {
            m_stackedWidget->setCurrentIndex(0);
        }
        AfcExplorerWidget *currentExplorer =
            qobject_cast<AfcExplorerWidget *>(m_stackedWidget->currentWidget());
        if (currentExplorer) {
            currentExplorer->navigateToPath(path);
        }
    }
}

void FileExplorerWidget::saveFavoritePlace(const QString &alias,
                                           const QString &path)
{
    qDebug() << "Saving favorite place:" << alias << "->" << path;
    SettingsManager *settings = SettingsManager::sharedInstance();
    settings->saveFavoritePlace(path, alias, "favorite_places/");
}

void FileExplorerWidget::saveFavoritePlaceAfc2(const QString &alias,
                                               const QString &path)
{
    SettingsManager *settings = SettingsManager::sharedInstance();
    settings->saveFavoritePlace(path, alias, "favorite_places_afc2/");
}

void FileExplorerWidget::onSidebarContextMenuRequested(const QPoint &pos)
{
    QTreeWidgetItem *item = m_sidebarTree->itemAt(pos);

    // Only show a context menu for items that are direct children of the
    // favorites list.
    if (!item || item->parent() != m_favoritePlacesItem) {
        return;
    }

    QVariant data = item->data(0, Qt::UserRole);
    if (!data.isValid()) {
        return;
    }

    QMenu contextMenu;
    QAction *removeAction = contextMenu.addAction("Remove from Favorites");
    QAction *selectedAction =
        contextMenu.exec(m_sidebarTree->viewport()->mapToGlobal(pos));

    if (selectedAction == removeAction) {
        QVariantMap dataMap = data.toMap();
        QString path = dataMap.value("path").toString();
        bool afc2 = dataMap.value("afc2").toBool();

        SettingsManager *settings = SettingsManager::sharedInstance();
        if (afc2) {
            settings->removeFavoritePlace("favorite_places_afc2/", path);
        } else {
            settings->removeFavoritePlace("favorite_places/", path);
        }
    }
}