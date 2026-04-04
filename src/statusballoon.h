#ifndef STATUSBALLOON_H
#define STATUSBALLOON_H

#include "iDescriptor-ui.h"
#include "iDescriptor.h"
#include "qballoontip.h"
#include <QBasicTimer>
#include <QDateTime>
#include <QIcon>
#include <QLabel>
#include <QMutex>
#include <QProgressBar>
#include <QPushButton>
#include <QSystemTrayIcon>
#include <QUuid>
#include <QVBoxLayout>
#include <QWidget>
#include <atomic>
#include <memory>

class BalloonProcess;

enum class ProcessType { Export, Import };

enum class ProcessStatus { Queued, Running, Completed, Failed, Cancelled };

struct ProcessItem {
    QUuid processId;
    ProcessType type;
    ProcessStatus status;
    QString title;
    QString currentFile;
    int totalItems = 0;
    int completedItems = 0;
    int failedItems = 0;
    qint64 totalBytes = 0;
    qint64 transferredBytes = 0;
    QDateTime startTime;
    QDateTime endTime;
    QString destinationPath;
    // QUuid jobId;

    BalloonProcess *processWidget = nullptr;
};

class BalloonProcess : public QWidget
{
    Q_OBJECT
public:
    explicit BalloonProcess(std::shared_ptr<ProcessItem> item,
                            QWidget *parent = nullptr);

    void updateUI();

private:
    void onCancelClicked();
    void enterEvent(QEnterEvent *event) override;
    void leaveEvent(QEvent *event) override;
    void onOpenFolderClicked();
    void updateStyles();

    std::shared_ptr<ProcessItem> m_item;
    QDateTime m_lastUpdateTime;
    qint64 m_lastBytesTransferred;

    QLabel *m_titleLabel;
    QLabel *m_statusLabel;
    QLabel *m_statsLabel;
    QLabel *m_currentFileLabel;
    QProgressBar *m_progressBar;
    QPushButton *m_actionButton;
    QPushButton *m_cancelButton;
    ZIconWidget *m_removeBtn;

protected:
    void changeEvent(QEvent *event) override
    {
        if (event->type() == QEvent::PaletteChange) {
            updateStyles();
        }
        QWidget::changeEvent(event);
    }
};

class StatusBalloon : public QBalloonTip
{
    Q_OBJECT
public:
    explicit StatusBalloon(QWidget *parent = nullptr);
    static StatusBalloon *sharedInstance();

    // Process management
    QUuid startProcess(const QString &title, int totalItems,
                       const QString &destinationPath, ProcessType type,
                       const QUuid &jobId);

    void onFileTransferProgress(const QUuid &processId,
                                const QString &currentFile,
                                qint64 bytesTransferred, qint64 totalBytes);

    bool hasActiveProcesses() const;
    void removeProcess(const QUuid &processId);

protected:
#ifdef WIN32
    void showEvent(QShowEvent *event) override;
#endif
    void resizeEvent(QResizeEvent *event) override;

private:
    void updateHeader();
    void handleShow(bool forceVisible = false);
    void createProcessWidget(std::shared_ptr<ProcessItem> item);
    void connectExportThreadSignals();
    void onExportJobFinished(const QUuid &job_id, bool cancelled,
                             qint64 successful_items, qint64 failed_items,
                             qint64 total_bytes);
    void onItemExported(const QUuid &job_id, const QString &file_name,
                        const QString &destination_path, bool success,
                        int bytes_transferred, const QString &error_message);
    void onImportJobFinished(const QUuid &job_id, bool cancelled,
                             qint64 successful_items, qint64 failed_items,
                             qint64 total_bytes);
    void onItemImported(const QUuid &job_id, const QString &file_name,
                        const QString &destination_path, bool success,
                        int bytes_transferred, const QString &error_message);
    void handleJobUpdate(const std::shared_ptr<ProcessItem> &item);

    QVBoxLayout *m_mainLayout;
    QLabel *m_headerLabel;
    QWidget *m_processesContainer;
    QVBoxLayout *m_processesLayout;

    QMap<QUuid, std::shared_ptr<ProcessItem>> m_processes;
    mutable QMutex m_processesMutex;
    QLabel *m_noProcesesLabel;
};
#endif // STATUSBALLOON_H
