#ifndef EXPORTALBUM_H
#define EXPORTALBUM_H

#include "appcontext.h"
#include "iDescriptor-ui.h"
#include "iDescriptor.h"
#include "iomanagerclient.h"
#include "qprocessindicator.h"
#include "zloadingwidget.h"
#include <QDialog>
#include <QMessageBox>
#include <QMutex>
#include <QObject>
#include <QThread>
#include <QTimer>
#include <QWidget>


struct ScanResult {
    bool ok;
    quint64 count;
    QStringList items;
};

class AlbumScanWorker : public QObject
{
    Q_OBJECT
public:
    explicit AlbumScanWorker(const std::shared_ptr<iDescriptorDevice> &device)
        : m_device(device)
    {
    }

public slots:
    void scanAlbums(const QStringList &paths);
    void calculateTotalSize(const QStringList &items);
    void cancel();

signals:
    void scanFinished(bool ok, quint64 count, const QStringList &items);
    void totalSizeProgress(quint64 totalSize);
    void totalSizeFinished(quint64 totalSize);

private:
    bool isCancelled();

    const std::shared_ptr<iDescriptorDevice> m_device;
    QMutex m_cancelMutex;
    bool m_cancelRequested = false;
};

class ExportAlbum : public QDialog
{
    Q_OBJECT
public:
    explicit ExportAlbum(const std::shared_ptr<iDescriptorDevice> device,
                         const QStringList &paths, QWidget *parent = nullptr);
    ~ExportAlbum();

signals:
    void requestScan(const QStringList &paths);
    void requestTotalSize(const QStringList &items);
    void requestCancelWorker();

private:
    QThread *m_workerThread = nullptr;
    AlbumScanWorker *m_worker = nullptr;
    ZLoadingWidget *m_loadingWidget;
    const std::shared_ptr<iDescriptorDevice> m_device;
    QLabel *m_infoLabel;
    size_t m_listCount;
    QList<QString> m_exportItems;
    ZDirPickerLabel *m_dirPickerLabel;
    QLabel *m_totalSizeExportLabel;
    QProcessIndicator *m_loadingIndicator = nullptr;
    quint64 m_totalExportSize = 0;
    bool m_exiting = false;
    void getTotalPhotoCount(const QStringList &paths);
    void updateInfoLabel(quint64 photoCount);
    // startExport(const QStringList &paths, const QString &exportDir);
    void startExport();
    void calculateTotalExportSize();
};

#endif // EXPORTALBUM_H
