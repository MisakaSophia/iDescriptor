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

#ifndef DEVDISKIMAGEHELPER_H
#define DEVDISKIMAGEHELPER_H

#include "iDescriptor.h"
#include <QDialog>
#include <QLabel>
#include <QPushButton>
#include <QWidget>

class QProcessIndicator;

class DevDiskImageHelper : public QDialog
{
    Q_OBJECT
public:
    explicit DevDiskImageHelper(iDescriptorDevice *device,
                                QWidget *parent = nullptr);

    // Start the mounting process
    void start();

signals:
    void mountingCompleted(bool success);
    void downloadStarted();
    void downloadCompleted(bool success);

private slots:
    void checkAndMount();
    void onMountButtonClicked();
    void onRetryButtonClicked();
    void onImageDownloadFinished(const QString &version, bool success,
                                 const QString &errorMessage);

private:
    void setupUI();
    void showStatus(const QString &message, bool isError = false);
    void showMountUI();
    void showRetryUI(const QString &errorMessage);
    void finishWithSuccess();
    void finishWithError(const QString &errorMessage);

    iDescriptorDevice *m_device;

    QLabel *m_statusLabel;
    QProcessIndicator *m_loadingIndicator;
    QPushButton *m_mountButton;
    QPushButton *m_retryButton;
    QPushButton *m_cancelButton;

    QString m_downloadingVersion;
};

#endif // DEVDISKIMAGEHELPER_H
