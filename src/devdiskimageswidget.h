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

#ifndef DEVDISKIMAGESWIDGET_H
#define DEVDISKIMAGESWIDGET_H

#include "devdiskimagehelper.h"
#include "iDescriptor-ui.h"
#include "iDescriptor.h"
#include "qprocessindicator.h"
#include <QComboBox>
#include <QLabel>
#include <QListWidget>
#include <QMap>
#include <QNetworkReply>
#include <QPair>
#include <QProgressBar>
#include <QPushButton>
#include <QStackedWidget>
#include <QStringList>
#include <QWidget>
#include <string>

class DevDiskImagesWidget : public Tool
{
    Q_OBJECT
public:
    explicit DevDiskImagesWidget(const QString &deviceUdid,
                                 QWidget *parent = nullptr);

private slots:
    void fetchImages();
    void onDownloadButtonClicked();
    void onDownloadProgress(qint64 bytesReceived, qint64 bytesTotal);
    void onFileDownloadFinished();
    void updateDeviceList();
    void onMountButtonClicked();
    void onImageListFetched(bool success,
                            const QString &errorMessage = QString());
    void onItemSelectionChanged();

private:
    void setupUi();
    void displayImages();
    void startDownload(const QString &version);
    void mountImage(const QString &version);
    void onDeviceSelectionChanged(int index);
    void closeEvent(QCloseEvent *event) override;
    void checkMountedImage();

    struct DownloadItem {
        QNetworkReply *dmgReply = nullptr;
        QNetworkReply *sigReply = nullptr;
        QProgressBar *progressBar = nullptr;
        QPushButton *downloadButton = nullptr;
        QString version;
        qint64 totalSize = 0;
        qint64 totalReceived = 0;
        qint64 dmgReceived = 0;
        qint64 sigReceived = 0;
    };

    std::string m_mounted_sig = "";
    uint64_t m_mounted_sig_len = 0;

    QStackedWidget *m_stackedWidget;
    QListWidget *m_imageListWidget;
    QLabel *m_statusLabel;
    QLabel *m_initialStatusLabel;
    QWidget *m_errorWidget;
    QComboBox *m_deviceComboBox;
    QPushButton *m_mountButton;
    QPushButton *m_check_mountedButton;
    QProcessIndicator *m_processIndicator;

    QString m_currentDeviceUdid;
    QStringList m_compatibleVersions;
    QStringList m_otherVersions;

    QMap<QString, QPair<QString, QString>>
        m_availableImages; // version -> {dmg_path, sig_path}
    QMap<QNetworkReply *, DownloadItem *> m_activeDownloads;
};

#endif // DEVDISKIMAGESWIDGET_H
