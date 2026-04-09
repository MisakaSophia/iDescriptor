#include "exportalbum.h"

void AlbumScanWorker::scanAlbums(const QStringList &paths)
{
    {
        QMutexLocker locker(&m_cancelMutex);
        m_cancelRequested = false;
    }

    ScanResult res{true, 0, {}};

    for (const QString &path : paths) {
        if (isCancelled()) {
            return;
        }

        QList<QString> items = m_device->afc_backend->list_files_flat(path);

        if (items.isEmpty()) {
            res.ok = false;
            continue;
        }

        for (const QString &item : items) {
            if (isCancelled()) {
                return;
            }
            if (item.isEmpty())
                continue;
            res.items.append(item);
        }
        res.count += static_cast<quint64>(items.size());
    }

    emit scanFinished(res.ok, res.count, res.items);
}

void AlbumScanWorker::calculateTotalSize(const QStringList &items)
{
    {
        QMutexLocker locker(&m_cancelMutex);
        m_cancelRequested = false;
    }

    quint64 totalSize = 0;

    for (const QString &item : items) {
        if (isCancelled()) {
            return;
        }

        int size = m_device->afc_backend->get_file_size(item);
        if (size > 0) {
            totalSize += static_cast<quint64>(size);
        }
        emit totalSizeProgress(totalSize);
    }

    emit totalSizeFinished(totalSize);
}

void AlbumScanWorker::cancel()
{
    QMutexLocker locker(&m_cancelMutex);
    m_cancelRequested = true;
}

bool AlbumScanWorker::isCancelled()
{
    QMutexLocker locker(&m_cancelMutex);
    return m_cancelRequested;
}

ExportAlbum::ExportAlbum(const std::shared_ptr<iDescriptorDevice> device,
                         const QStringList &paths, QWidget *parent)
    : QDialog(parent), m_device(device), m_listCount(paths.size())
{
    setWindowTitle("Export Album");
    setMaximumSize(600, 400);
#ifdef WIN32
    setupWinWindow(this);
#endif

    m_loadingWidget = new ZLoadingWidget(false, this);
    QVBoxLayout *mainLayout = new QVBoxLayout(this);
    mainLayout->addWidget(m_loadingWidget);

    m_workerThread = new QThread(this);
    m_worker = new AlbumScanWorker(m_device);
    m_worker->moveToThread(m_workerThread);

    connect(m_workerThread, &QThread::finished, m_worker,
            &QObject::deleteLater);
    connect(this, &ExportAlbum::requestScan, m_worker,
            &AlbumScanWorker::scanAlbums, Qt::QueuedConnection);
    connect(this, &ExportAlbum::requestTotalSize, m_worker,
            &AlbumScanWorker::calculateTotalSize, Qt::QueuedConnection);
    connect(this, &ExportAlbum::requestCancelWorker, m_worker,
            &AlbumScanWorker::cancel, Qt::QueuedConnection);

    connect(m_worker, &AlbumScanWorker::scanFinished, this,
            [this](bool ok, quint64 count, const QStringList &items) {
                qDebug() << "Total photo count:" << count << "with"
                         << (ok ? 0 : 1) << "errors";

                if (m_exiting) {
                    return;
                }

                if (ok) {
                    m_exportItems = items;
                    updateInfoLabel(count);
                    calculateTotalExportSize();
                    m_loadingWidget->stop();
                } else {
                    QMessageBox::warning(
                        nullptr, "Error",
                        "Failed to read directory: cannot export album(s)");
                    reject();
                }
            });

    connect(
        m_worker, &AlbumScanWorker::totalSizeProgress, this,
        [this](quint64 totalSize) {
            if (m_exiting) {
                return;
            }
            m_totalExportSize = totalSize;
            m_totalSizeExportLabel->setText(
                QString("Total size to export: %1")
                    .arg(iDescriptor::Utils::formatSize(m_totalExportSize)));
        });

    connect(
        m_worker, &AlbumScanWorker::totalSizeFinished, this,
        [this](quint64 totalSize) {
            if (m_exiting) {
                return;
            }
            m_totalExportSize = totalSize;
            m_totalSizeExportLabel->setText(
                QString("Total size to export: %1")
                    .arg(iDescriptor::Utils::formatSize(m_totalExportSize)));
            m_loadingIndicator->stop();
            m_loadingIndicator->hide();
        });

    m_workerThread->start();

    connect(AppContext::sharedInstance(), &AppContext::deviceRemoved, this,
            [this](const QString &udid) {
                if (udid == m_device->udid) {
                    m_exiting = true;
                    emit requestCancelWorker();
                    QTimer::singleShot(0, this, [this]() { close(); });
                }
            });

    QWidget *contentWidget = new QWidget(this);
    QVBoxLayout *contentLayout = new QVBoxLayout(contentWidget);

    m_infoLabel = new QLabel(this);
    contentLayout->addWidget(m_infoLabel);

    QHBoxLayout *buttonLayout = new QHBoxLayout();
    QPushButton *cancelButton = new QPushButton("Cancel", this);
    QPushButton *exportButton = new QPushButton("Export", this);
    buttonLayout->addWidget(exportButton);
    buttonLayout->addWidget(cancelButton);
    m_dirPickerLabel = new ZDirPickerLabel();

    contentLayout->addWidget(m_dirPickerLabel);

    QHBoxLayout *sizeLayout = new QHBoxLayout();

    m_totalSizeExportLabel = new QLabel("Total size to export: 0 MB", this);
    sizeLayout->addWidget(m_totalSizeExportLabel);

    m_loadingIndicator = new QProcessIndicator(this);
    m_loadingIndicator->setType(QProcessIndicator::line_rotate);
    m_loadingIndicator->setFixedSize(32, 16);
    sizeLayout->addWidget(m_loadingIndicator);
    sizeLayout->addStretch();

    contentLayout->addLayout(sizeLayout);
    contentLayout->addLayout(buttonLayout);

    connect(cancelButton, &QPushButton::clicked, this, [this]() {
        m_exiting = true;
        emit requestCancelWorker();
        QTimer::singleShot(0, this, [this]() { close(); });
    });
    connect(exportButton, &QPushButton::clicked, this, [this, exportButton]() {
        m_exiting = true;
        emit requestCancelWorker();
        exportButton->setEnabled(false);
        QTimer::singleShot(0, this, [this]() {
            startExport();
            accept();
        });
    });

    m_loadingWidget->setupContentWidget(contentWidget);

    getTotalPhotoCount(paths);

    connect(this, &QDialog::finished, this, [this](int) {
        m_exiting = true;
        deleteLater();
    });
}

void ExportAlbum::getTotalPhotoCount(const QStringList &paths)
{
    emit requestScan(paths);
}

void ExportAlbum::updateInfoLabel(quint64 photoCount)
{
    m_infoLabel->setText(QString("Are you sure you want to export %1 album(s) "
                                 "with %2 photo(s)/video(s) ?")
                             .arg(m_listCount)
                             .arg(photoCount));
}

void ExportAlbum::startExport()
{
    IOManagerClient::sharedInstance()->startExport(
        m_device, m_exportItems, m_dirPickerLabel->getOutputDir(),
        "Exporting Album(s)");
}

void ExportAlbum::calculateTotalExportSize()
{
    m_totalExportSize = 0;
    m_totalSizeExportLabel->setText("Total size to export: 0 MB");
    m_loadingIndicator->start();
    emit requestTotalSize(m_exportItems);
}

ExportAlbum::~ExportAlbum()
{
    m_exiting = true;
    emit requestCancelWorker();

    if (m_workerThread && m_workerThread->isRunning()) {
        m_workerThread->quit();
        m_workerThread->wait();
    }
}