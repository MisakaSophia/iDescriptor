#include "exportalbum.h"

ExportAlbum::ExportAlbum(const iDescriptorDevice *device,
                         const QStringList &paths, QWidget *parent)
    : QDialog(parent), m_device(device), m_listCount(paths.size())
{
    setWindowTitle("Export Album");
    setMaximumSize(600, 400);
#ifdef WIN32
    setupWinWindow(this);
#endif

    m_loadingWidget = new ZLoadingWidget(true, this);
    QVBoxLayout *mainLayout = new QVBoxLayout(this);
    mainLayout->addWidget(m_loadingWidget);

    getTotalPhotoCount(paths);
    connect(AppContext::sharedInstance(), &AppContext::deviceRemoved, this,
            [this](const std::string &udid, const std::string &macAddress,
                   const std::string &ipAddress, bool wasWireless) {
                if (udid == m_device->udid) {
                    m_exiting = true;
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
    m_dirPickerLabel = new DirPickerLabel(this);

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
        QTimer::singleShot(0, this, [this]() { close(); });
    });
    connect(exportButton, &QPushButton::clicked, this, [this, exportButton]() {
        m_exiting = true;
        exportButton->setEnabled(false);
        QTimer::singleShot(0, this, [this]() {
            startExport();
            accept();
        });
    });

    m_loadingWidget->setupContentWidget(contentWidget);

    connect(this, &QDialog::finished, this, [this](int) {
        m_exiting = true;
        deleteLater();
    });
}

void ExportAlbum::getTotalPhotoCount(const QStringList &paths)
{
    QFutureWatcher<std::pair<bool, size_t>> *watcher =
        new QFutureWatcher<std::pair<bool, size_t>>(this);

    connect(watcher, &QFutureWatcher<std::pair<bool, size_t>>::finished, this,
            [this, watcher]() {
                std::pair<bool, size_t> result = watcher->result();
                qDebug() << "Total photo count:" << result.second << "with"
                         << (result.first ? 0 : 1) << "errors";

                if (result.first) {
                    updateInfoLabel(result.second);
                    calculateTotalExportSize();
                    m_loadingWidget->stop();
                } else {
                    QMessageBox::warning(
                        nullptr, "Error",
                        "Failed to read directory: cannot export album(s)");
                    reject();
                }
            });

    watcher->setFuture(QtConcurrent::run([this, paths]() {
        size_t count = 0;
        bool errorOccurred = false;
        for (const QString &path : paths) {
            size_t innerCount = 0;
            char **items = nullptr;

            IdeviceFfiError *err = ServiceManager::safeAfcReadDirectory(
                m_device, path.toStdString().c_str(), &items, &innerCount);

            if (err) {
                qDebug() << "Failed to read directory:"
                         << path.toStdString().c_str()
                         << "Error:" << err->message << "Code:" << err->code;

                errorOccurred = true;
                idevice_error_free(err);
            } else {
                for (size_t i = 0; i < innerCount; ++i) {
                    const char *item = items[i];
                    if (!item) {
                        continue;
                    }
                    QString fileName = QString::fromUtf8(item);

                    if (fileName.endsWith(".") || fileName.endsWith("..")) {
                        continue;
                    }

                    QString filePath = path + "/" + QString::fromUtf8(item);

                    m_exportItems.append(
                        ExportItem(filePath, fileName, m_device->udid));
                }
                free_directory_listing(items, innerCount);
                count += innerCount;
            }
        }
        return std::make_pair(!errorOccurred, count);
    }));
}

void ExportAlbum::updateInfoLabel(size_t photoCount)
{
    m_infoLabel->setText(QString("Are you sure you want to export %1 album(s) "
                                 "with %2 photo(s)/video(s) ?")
                             .arg(m_listCount)
                             .arg(photoCount));
}

void ExportAlbum::startExport()
{
    ExportManager::sharedInstance()->startExport(
        m_device, m_exportItems, m_dirPickerLabel->getOutputDir(),
        "Exporting Album(s)");
}

void ExportAlbum::calculateTotalExportSize()
{
    m_totalExportSize = 0;
    m_loadingIndicator->start();

    auto timer = new QTimer(this);
    timer->setInterval(500);

    connect(timer, &QTimer::timeout, this, [this]() {
        m_totalSizeExportLabel->setText(
            QString("Total size to export: %1")
                .arg(iDescriptor::Utils::formatSize(m_totalExportSize.load())));
    });

    timer->start();

    QThreadPool::globalInstance()->start([this, timer]() {
        for (const ExportItem &item : m_exportItems) {
            if (m_exiting.load()) {
                return;
            }
            AfcFileInfo info = {};
            IdeviceFfiError *err = ServiceManager::safeAfcGetFileInfo(
                m_device, item.sourcePathOnDevice.toStdString().c_str(), &info);

            if (err) {
                qDebug() << "Failed to get file info for:"
                         << item.sourcePathOnDevice << "Error:" << err->message
                         << "Code:" << err->code;
                idevice_error_free(err);
            } else {
                this->m_totalExportSize += info.size;
                afc_file_info_free(&info);
            }
        }

        QMetaObject::invokeMethod(
            this,
            [this, timer]() {
                if (m_exiting.load()) {
                    return;
                }
                timer->stop();
                timer->deleteLater();
                this->m_totalSizeExportLabel->setText(
                    QString("Total size to export: %1")
                        .arg(iDescriptor::Utils::formatSize(
                            this->m_totalExportSize.load())));
                this->m_loadingIndicator->stop();
                this->m_loadingIndicator->hide();
            },
            Qt::QueuedConnection);
    });
}