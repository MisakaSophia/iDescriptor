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

#include "exportmanager.h"
#include "servicemanager.h"
#include "statusballoon.h"

ExportManager *ExportManager::sharedInstance()
{
    static ExportManager self;
    return &self;
}
// TODO: unfinished

ExportManager::ExportManager(QObject *parent) : QObject(parent) {}

// FIXME
ExportManager::~ExportManager()
{
    // Cancel all active jobs
    QMutexLocker locker(&m_jobsMutex);
    // for (auto jobPtr : m_activeJobs) {
    //     jobPtr->cancelRequested = true;
    //     if (jobPtr->watcher) {
    //         jobPtr->watcher->cancel();
    //         jobPtr->watcher->waitForFinished();
    //     }
    //     // delete jobPtr;
    // }
    m_activeJobs.clear();
}

QUuid ExportManager::startExport(const iDescriptorDevice *device,
                                 const QList<ExportItem> &items,
                                 const QString &destinationPath,
                                 const QString &exportTitle,
                                 std::optional<AfcClientHandle *> altAfc)
{
    qDebug() << "startExport() entry - items:" << items.size()
             << "dest:" << destinationPath;
    if (!device) {
        qWarning() << "Invalid device provided to ExportManager";
        QMessageBox::critical(nullptr, "Export Error",
                              "Invalid device specified for export.");
        return QUuid();
    }

    if (items.isEmpty()) {
        qWarning() << "No items provided for export";
        QMessageBox::information(nullptr, "Export Error",
                                 "No items selected for export.");
        return QUuid();
    }

    // Validate destination directory
    QDir destDir(destinationPath);
    if (!destDir.exists()) {
        if (!destDir.mkpath(".")) {
            qWarning() << "Could not create destination directory:"
                       << destinationPath;
            QMessageBox::critical(nullptr, "Export Error",
                                  "Could not create destination directory.");

            return QUuid();
        }
    }

    // Create new job
    auto job = new ExportJob();
    job->jobId = QUuid::createUuid();
    job->items = items;
    job->destinationPath = destinationPath;
    job->altAfc = altAfc;
    job->d_udid = device->udid;

    job->statusBalloonProcessId = StatusBalloon::sharedInstance()->startProcess(
        exportTitle, items.size(), destinationPath, ProcessType::Export,
        job->jobId);

    // todo:cleanupJob ?
    // connect(job->watcher, &QFutureWatcher<void>::finished, this,
    //         [this, managerJobId]() { cleanupJob(managerJobId); });

    // Store job before starting
    {
        QMutexLocker locker(&m_jobsMutex);
        m_activeJobs[job->jobId] = job;
    }

    m_exportThread->executeExportJob(job);
    qDebug() << "Started export job" << job->jobId << "for" << items.size()
             << "items";
    return job->jobId;
}

QUuid ExportManager::startImport(const iDescriptorDevice *device,
                                 const QList<ImportItem> &items,
                                 const QString &destinationPath,
                                 const QString &importTitle,
                                 std::optional<AfcClientHandle *> altAfc)
{
    qDebug() << "startExport() entry - items:" << items.size()
             << "dest:" << destinationPath;
    if (!device) {
        qWarning() << "Invalid device provided to ExportManager";
        QMessageBox::critical(nullptr, "Import Error",
                              "Invalid device specified for import.");
        return QUuid();
    }

    if (items.isEmpty()) {
        qWarning() << "No items provided for export";
        QMessageBox::information(nullptr, "Import Error",
                                 "No items selected for import.");
        return QUuid();
    }

    // Create new job
    auto job = new ImportJob();
    job->jobId = QUuid::createUuid();
    job->items = items;
    job->destinationPath = destinationPath;
    job->altAfc = altAfc;
    job->d_udid = device->udid;

    job->statusBalloonProcessId = StatusBalloon::sharedInstance()->startProcess(
        importTitle, items.size(), destinationPath, ProcessType::Import,
        job->jobId);

    // todo:cleanupJob ?
    // connect(job->watcher, &QFutureWatcher<void>::finished, this,
    //         [this, managerJobId]() { cleanupJob(managerJobId); });

    // Store job before starting
    {
        QMutexLocker locker(&m_jobsMutex);
        m_activeJobs[job->jobId] = job;
    }

    m_exportThread->executeImportJob(job);
    qDebug() << "Started import job" << job->jobId << "for" << items.size()
             << "items";
    return job->jobId;
}

void ExportManager::cancel(const QUuid &jobId)
{
    QMutexLocker locker(&m_jobsMutex);
    auto it = m_activeJobs.find(jobId);
    if (it != m_activeJobs.end()) {
        it.value()->cancelRequested = true;
        qDebug() << "Cancellation requested for job" << jobId;
    }
}

bool ExportManager::isJobRunning(const QUuid &jobId) const
{
    QMutexLocker locker(&m_jobsMutex);
    return m_activeJobs.contains(jobId);
}

// TODO: is not being used ?
QString ExportManager::extractFileName(const QString &devicePath) const
{
    int lastSlash = devicePath.lastIndexOf('/');
    if (lastSlash != -1 && lastSlash < devicePath.length() - 1) {
        return devicePath.mid(lastSlash + 1);
    }
    return devicePath;
}

void ExportManager::cleanupJob(const QUuid &jobId)
{
    QMutexLocker locker(&m_jobsMutex);
    auto it = m_activeJobs.find(jobId);
    // if (it != m_activeJobs.end()) {
    //     if (it.value()->watcher) {
    //         it.value()->watcher->deleteLater();
    //     }

    //     // delete it.value();
    //     m_activeJobs.erase(it);
    //     qDebug() << "Cleaned up export job" << jobId;
    // }
}

void ExportManager::cancelAllJobs()
{
    QMutexLocker locker(&m_jobsMutex);
    for (auto jobPtr : m_activeJobs) {
        if (jobPtr)
            jobPtr->cancelRequested = true;
    }
    qDebug() << "Cancellation requested for all active jobs";
}