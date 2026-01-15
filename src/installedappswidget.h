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

#ifndef INSTALLEDAPPSWIDGET_H
#define INSTALLEDAPPSWIDGET_H

#include "iDescriptor.h"
#include "zlineedit.h"
#include <QCheckBox>
#include <QEnterEvent>
#include <QFrame>
#include <QFutureWatcher>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QProgressBar>
#include <QPushButton>
#include <QScrollArea>
#include <QSplitter>
#include <QStackedWidget>
#include <QVBoxLayout>
#include <QWidget>

// Custom App Tab Widget
class AppTabWidget : public QGroupBox
{
    Q_OBJECT

public:
    AppTabWidget(const QString &appName, const QString &bundleId,
                 const QString &version, const QPixmap &icon = QPixmap(),
                 QWidget *parent = nullptr);

    void setSelected(bool selected);
    bool isSelected() const { return m_selected; }

    QString getBundleId() const { return m_bundleId; }
    QString getAppName() const { return m_appName; }
    QString getVersion() const { return m_version; }
    void updateStyles();

signals:
    void clicked();

protected:
    void mousePressEvent(QMouseEvent *event) override;

private:
    void setupUI();

    QString m_appName;
    QString m_bundleId;
    QString m_version;
    bool m_selected = false;

    QLabel *m_iconLabel;
    QLabel *m_nameLabel;
    QLabel *m_versionLabel;
    QList<AppTabWidget *> m_appTabs;
    QNetworkAccessManager *m_networkManager = new QNetworkAccessManager(this);
};

class InstalledAppsWidget : public QWidget
{
    Q_OBJECT

public:
    explicit InstalledAppsWidget(iDescriptorDevice *device,
                                 QWidget *parent = nullptr);
    ~InstalledAppsWidget();

private slots:
    void onAppsDataReady();
    void onAppTabClicked();
    void onContainerDataReady();
    void onFileSharingFilterChanged(bool enabled);

private:
    void setupUI();
    void createLoadingWidget();
    void createErrorWidget();
    void createContentWidget();
    void createLeftPanel();
    void createRightPanel();
    void fetchInstalledApps();
    void createAppTab(const QString &appName, const QString &bundleId,
                      const QString &version, const QPixmap &icon = QPixmap());
    void showLoadingState();
    void showErrorState(const QString &error);
    void selectAppTab(AppTabWidget *tab);
    void filterApps(const QString &searchText);
    void loadAppContainer(const QString &bundleId);
    void cleanupHouseArrestClients();

    iDescriptorDevice *m_device;
    QHBoxLayout *m_mainLayout;
    QStackedWidget *m_stackedWidget;
    QWidget *m_loadingWidget;
    QWidget *m_errorWidget;
    QWidget *m_contentWidget;
    QLabel *m_errorLabel;
    ZLineEdit *m_searchEdit;
    QCheckBox *m_fileSharingCheckBox;
    QScrollArea *m_tabScrollArea;
    QWidget *m_tabContainer;
    QVBoxLayout *m_tabLayout;
    QProgressBar *m_progressBar;
    QScrollArea *m_containerScrollArea;
    QWidget *m_containerWidget;
    QVBoxLayout *m_containerLayout;
    QFutureWatcher<QVariantMap> *m_watcher;
    QFutureWatcher<QVariantMap> *m_containerWatcher;
    QSplitter *m_splitter;
    HouseArrestClientHandle *m_houseArrestClient = nullptr;
    AfcClientHandle *m_houseArrestAfcClient = nullptr;
    // App data storage
    QList<AppTabWidget *> m_appTabs;
    AppTabWidget *m_selectedTab = nullptr;
};

#endif // INSTALLEDAPPSWIDGET_H