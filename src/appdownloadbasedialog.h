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

#ifndef APPDOWNLOADBASEDIALOG_H
#define APPDOWNLOADBASEDIALOG_H

#include <QDialog>
#include <QNetworkAccessManager>
#include <QProcess>
#include <QProgressBar>
#include <QPushButton>
#include <QVBoxLayout>

class AppDownloadBaseDialog : public QDialog
{
    Q_OBJECT
public:
    explicit AppDownloadBaseDialog(const QString &appName,
                                   const QString &bundleId,
                                   QWidget *parent = nullptr);

    QNetworkAccessManager *m_manager = new QNetworkAccessManager(this);
    QPushButton *m_actionButton;
    QProgressBar *m_progressBar;
    QTimer *m_progressTimer;
    QProcess *m_downloadProcess;
    QString m_appName;
    QVBoxLayout *m_layout;

public slots:
    void updateProgressBar(int percentage);

signals:
    void downloadFinished(bool success, const QString &message);

protected:
    void reject() override;
    void startDownloadProcess(const QString &bundleId,
                              const QString &workingDir, int index,
                              bool promptToOpenDir = true, bool close = false);
    void checkDownloadProgress(const QString &logFilePath,
                               const QString &appName,
                               const QString &outputDir);
    void addProgressBar(int index);
    void tryToDownload(const QString &bundleId, const QString &outputDir,
                       bool promptToOpenDir, bool close = false);

private:
    bool m_operationInProgress = false;
    QString m_bundleId;
    uint m_tries = 0;
private slots:
    void cleanup();
};

#endif // APPDOWNLOADBASEDIALOG_H
