#include "exportmanagerthread.h"
#include "appcontext.h"
#include "iDescriptor.h"
#include "servicemanager.h"
#include <QDebug>
#include <QDir>
#include <QThread>
#include <QtConcurrent>

// TODO: unfinished
void ExportManagerThread::executeExportJob(ExportJob *job)
{
    const QString udid = QString::fromStdString(job->d_udid);

    QMutexLocker locker(&m_queueMutex);
    QueuedJob q;
    q.type = QueuedJob::Type::Export;
    q.exportJob = job;

    auto &queue = m_deviceQueues[udid];
    queue.enqueue(q);

    if (!m_deviceBusy.contains(udid)) {
        m_deviceBusy.insert(udid);
        startNextJobLocked(udid);
    }
}

void ExportManagerThread::executeExportJobInternal(ExportJob *job)
{
    qDebug() << "Worker thread started for export job" << job->jobId;
    ExportJobSummary summary;
    summary.jobId = job->jobId;
    summary.totalItems = job->items.size();
    summary.destinationPath = job->destinationPath;

    qDebug() << "Executing export job" << job->jobId << "with"
             << job->items.size() << "items";

    for (int i = 0; i < job->items.size(); ++i) {
        if (job->cancelRequested.load()) {
            summary.wasCancelled = true;
            goto exit;
        }

        const ExportItem &item = job->items.at(i);

        ExportResult result =
            exportSingleItem(item, job->destinationPath, job->altAfc,
                             job->cancelRequested, job->statusBalloonProcessId);
        if (result.success) {
            summary.successfulItems++;
            summary.totalBytesTransferred += result.bytesTransferred;
        } else {
            summary.failedItems++;
        }

        if (result.cancelled) {
            summary.wasCancelled = true;
            goto exit;
        }

        emit itemExported(job->statusBalloonProcessId, result);
    }

exit:
    qDebug() << "Export job" << job->jobId
             << "completed - Success:" << summary.successfulItems
             << "Failed:" << summary.failedItems
             << "Bytes:" << summary.totalBytesTransferred;

    emit exportFinished(job->statusBalloonProcessId, summary);
}

ExportResult ExportManagerThread::exportSingleItem(
    const ExportItem &item, const QString &destinationDir,
    std::optional<AfcClientHandle *> altAfc, std::atomic<bool> &cancelRequested,
    const QUuid &statusBalloonProcessId)
{
    ExportResult result; //
    result.sourceFilePath = item.sourcePathOnDevice;

    // Generate output path
    QString outputPath = QDir(destinationDir).filePath(item.suggestedFileName);
    // todo problem
    outputPath = generateUniqueOutputPath(outputPath);
    result.outputFilePath = outputPath;

    // Progress callback
    const QString &currentFile = item.suggestedFileName;
    auto progressCallback = [this, statusBalloonProcessId,
                             currentFile](qint64 transferred, qint64 total) {
        qDebug() << "Export progress-transferred:" << transferred
                 << "total:" << total;
        emit fileTransferProgress(statusBalloonProcessId, currentFile,
                                  transferred, total);
    };

    qDebug() << "About to export file from device:" << item.sourcePathOnDevice
             << "to" << outputPath;

    iDescriptorDevice *device =
        AppContext::sharedInstance()->getDevice(item.d_udid);

    // FIXME: is this way we do it?
    if (!device) {
        result.errorMessage = QString("Device with UDID %1 not found")
                                  .arg(QString::fromStdString(item.d_udid));
        qDebug() << result.errorMessage;
        return result;
    }

    // Export file using ServiceManager
    IdeviceFfiError *err = ServiceManager::exportFileToPath(
        device, item.sourcePathOnDevice.toUtf8().constData(),
        outputPath.toUtf8().constData(), progressCallback, &cancelRequested,
        altAfc);

    if (err != nullptr) {
        result.errorMessage =
            QString("Failed to export file: %1").arg(err->message);
        qDebug() << result.errorMessage;
        if (cancelRequested.load()) {
            result.cancelled = true;
        }
        // FIXME: THIS WILL FREE C STRING FROM RUST ??
        // because error may not be returned from rust, but from C++ wrapper
        // idevice_error_free(err);
        return result;
    }

    // Get file size for statistics
    QFileInfo fileInfo(outputPath);
    result.bytesTransferred = fileInfo.size();
    result.success = true;

    return result;
}

void ExportManagerThread::executeImportJob(ImportJob *job)
{
    const QString udid = QString::fromStdString(job->d_udid);

    QMutexLocker locker(&m_queueMutex);
    QueuedJob q;
    q.type = QueuedJob::Type::Import;
    q.importJob = job;

    auto &queue = m_deviceQueues[udid];
    queue.enqueue(q);

    if (!m_deviceBusy.contains(udid)) {
        m_deviceBusy.insert(udid);
        startNextJobLocked(udid);
    }
}

void ExportManagerThread::executeImportJobInternal(ImportJob *job)
{
    qDebug() << "Worker thread started for import job" << job->jobId;
    ExportJobSummary summary;
    summary.jobId = job->jobId;
    summary.totalItems = job->items.size();
    summary.destinationPath = job->destinationPath;

    qDebug() << "Executing import job" << job->jobId << "with"
             << job->items.size() << "items";

    for (int i = 0; i < job->items.size(); ++i) {
        if (job->cancelRequested.load()) {
            summary.wasCancelled = true;
            qDebug() << "Import job" << job->jobId << "was cancelled";

            emit exportCancelled(job->jobId);
            return;
        }

        const ImportItem &item = job->items.at(i);

        ImportResult result =
            importSingleItem(item, job->destinationPath, job->altAfc,
                             job->cancelRequested, job->statusBalloonProcessId);
        if (result.success) {
            summary.successfulItems++;
            summary.totalBytesTransferred += result.bytesTransferred;
        } else {
            summary.failedItems++;
        }

        emit itemImported(job->statusBalloonProcessId, result);
    }

    qDebug() << "Import job" << job->jobId
             << "completed - Success:" << summary.successfulItems
             << "Failed:" << summary.failedItems
             << "Bytes:" << summary.totalBytesTransferred;

    emit exportFinished(job->jobId, summary);
}

ImportResult ExportManagerThread::importSingleItem(
    const ImportItem &item, const QString &destinationDir,
    std::optional<AfcClientHandle *> altAfc, std::atomic<bool> &cancelRequested,
    const QUuid &statusBalloonProcessId)
{
    ImportResult result;
    result.sourceFilePath = item.sourcePathOnDevice;

    // Generate output path
    QString outputPath = QDir(destinationDir).filePath(item.suggestedFileName);
    outputPath = generateUniqueOutputPath(outputPath);
    result.outputFilePath = outputPath;

    // Progress callback
    const QString &currentFile = item.suggestedFileName;
    auto progressCallback = [this, statusBalloonProcessId,
                             currentFile](qint64 transferred, qint64 total) {
        qDebug() << "Import progress-transferred:" << transferred
                 << "total:" << total;
        emit fileTransferProgress(statusBalloonProcessId, currentFile,
                                  transferred, total);
    };

    qDebug() << "About to import file from device:" << item.sourcePathOnDevice
             << "to" << outputPath;

    iDescriptorDevice *device =
        AppContext::sharedInstance()->getDevice(item.d_udid);

    if (!device) {
        result.errorMessage = QString("Device with UDID %1 not found")
                                  .arg(QString::fromStdString(item.d_udid));
        qDebug() << result.errorMessage;
        return result;
    }

    // Import file using ServiceManager
    IdeviceFfiError *err = ServiceManager::importFileToPath(
        device, item.sourcePathOnDevice.toUtf8().constData(),
        outputPath.toUtf8().constData(), progressCallback, &cancelRequested,
        altAfc);

    if (err != nullptr) {
        result.errorMessage =
            QString("Failed to import file: %1").arg(err->message);
        qDebug() << result.errorMessage;
        idevice_error_free(err);
        return result;
    }

    // Get file size for statistics
    QFileInfo fileInfo(outputPath);
    result.bytesTransferred = fileInfo.size();
    result.success = true;

    return result;
}

QString ExportManagerThread::generateUniqueOutputPath(const QString &basePath)
{
    if (!QFile::exists(basePath)) {
        return basePath;
    }

    QFileInfo fileInfo(basePath);
    QString baseName = fileInfo.completeBaseName();
    QString suffix = fileInfo.suffix();
    QString directory = fileInfo.absolutePath();

    int counter = 1;
    QString uniquePath;

    do {
        QString newName = QString("%1_%2").arg(baseName).arg(counter);
        if (!suffix.isEmpty()) {
            newName += "." + suffix;
        }
        uniquePath = QDir(directory).filePath(newName);
        counter++;
    } while (QFile::exists(uniquePath) && counter < 10000);

    return uniquePath;
}

void ExportManagerThread::startNextJobLocked(const QString &udid)
{
    auto it = m_deviceQueues.find(udid);
    if (it == m_deviceQueues.end() || it->isEmpty()) {
        m_deviceQueues.remove(udid);
        m_deviceBusy.remove(udid);
        return;
    }

    QueuedJob job = it->head();

    QtConcurrent::run([this, udid, job]() {
        if (job.type == QueuedJob::Type::Export) {
            executeExportJobInternal(job.exportJob);
        } else {
            executeImportJobInternal(job.importJob);
        }

        // schedule dequeue, start on this object's thread
        QMetaObject::invokeMethod(
            this,
            [this, udid]() {
                QMutexLocker locker(&m_queueMutex);
                auto it = m_deviceQueues.find(udid);
                if (it != m_deviceQueues.end() && !it->isEmpty()) {
                    it->dequeue();
                }
                startNextJobLocked(udid);
            },
            Qt::QueuedConnection);
    });
}