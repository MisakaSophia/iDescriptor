#include "statusballoon.h"
#include "appcontext.h"
#include "iDescriptor.h"
#include "qballoontip.h"
#include <QApplication>
#include <QBasicTimer>
#include <QDateTime>
#include <QDebug>
#include <QDesktopServices>
#include <QGraphicsDropShadowEffect>
#include <QGridLayout>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QMutexLocker>
#include <QPainter>
#include <QPropertyAnimation>
#include <QPushButton>
#include <QScreen>
#include <QShowEvent>
#include <QStyle>
#include <QTimer>
#include <QTimerEvent>
#include <QUrl>
#include <QUuid>
#include <qpainterpath.h>

#ifdef WIN32
#include "platform/windows/win_common.h"
#endif

BalloonProcess::BalloonProcess(std::shared_ptr<ProcessItem> item,
                               QWidget *parent)
    : QWidget(parent), m_item(std::move(item))
{
    auto *layout = new QVBoxLayout(this);
    layout->setSpacing(5);
    layout->setContentsMargins(15, 5, 5, 15);

    m_lastBytesTransferred = 0;
    m_lastUpdateTime = QDateTime::currentDateTime();

    // Title
    m_titleLabel = new QLabel(m_item->title);
    QFont titleFont = m_titleLabel->font();
    m_titleLabel->setWordWrap(true);
    titleFont.setBold(true);
    m_titleLabel->setFont(titleFont);

    QHBoxLayout *titleLayout = new QHBoxLayout();
    titleLayout->addWidget(m_titleLabel);
    titleLayout->addStretch();

    m_removeBtn = new ZIconWidget(
        QIcon(":/resources/icons/MaterialSymbolsCloseRounded.png"), "Remove");
    auto *opacity = new QGraphicsOpacityEffect(m_removeBtn);
    opacity->setOpacity(0.0);
    m_removeBtn->setGraphicsEffect(opacity);

    m_removeBtn->setEnabled(false);

    connect(m_removeBtn, &ZIconWidget::clicked, this, [this]() {
        StatusBalloon::sharedInstance()->removeProcess(m_item->processId);
    });
    titleLayout->addWidget(m_removeBtn);

    layout->addLayout(titleLayout);

    // Status
    m_statusLabel = new QLabel("Starting...");
    layout->addWidget(m_statusLabel);

    // Progress bar
    m_progressBar = new QProgressBar();
#ifdef __APPLE__
    m_progressBar->setStyleSheet(QString("QProgressBar {"
                                         "    border-radius: 4px;"
                                         "    background: #eee;"
                                         "}"
                                         "QProgressBar::chunk {"
                                         "    background-color: %1;"
                                         "    border-radius: 4px;"
                                         "}")
                                     .arg(COLOR_ACCENT_BLUE.name()));
#endif
    m_progressBar->setRange(0, 100);
    m_progressBar->setValue(0);
    m_progressBar->setTextVisible(false);
    m_progressBar->setFixedHeight(12);
    layout->addWidget(m_progressBar);

    // Current file
    m_currentFileLabel = new QLabel();
    m_currentFileLabel->setWordWrap(true);
    QFont currentFileFont = m_currentFileLabel->font();
    currentFileFont.setPointSize(currentFileFont.pointSize() - 1);
    m_currentFileLabel->setFont(currentFileFont);
    layout->addWidget(m_currentFileLabel);

    // Stats
    m_statsLabel = new QLabel();
    QFont statsFont = m_statsLabel->font();
    statsFont.setPointSize(statsFont.pointSize() - 2);
    m_statsLabel->setFont(statsFont);
    layout->addWidget(m_statsLabel);

    // Buttons layout
    auto *buttonsLayout = new QHBoxLayout();
    buttonsLayout->setSpacing(6);

    // Action button
    m_actionButton = new QPushButton();
    m_actionButton->setVisible(false);
    if (m_item->type == ProcessType::Export) {
        m_actionButton->setText("Open Folder");
        connect(m_actionButton, &QPushButton::clicked, this,
                &BalloonProcess::onOpenFolderClicked);
    }
    buttonsLayout->addWidget(m_actionButton);

    buttonsLayout->addStretch();

    // Cancel button
    m_cancelButton = new QPushButton("Cancel");
    connect(m_cancelButton, &QPushButton::clicked, this,
            &BalloonProcess::onCancelClicked);
    buttonsLayout->addWidget(m_cancelButton);

    layout->addLayout(buttonsLayout);
    layout->addStretch();

    setObjectName("BalloonProcess");
    setAttribute(Qt::WA_StyledBackground, true);
    updateStyles();
}

void BalloonProcess::updateStyles()
{
    QString style;
    bool dark = isDarkMode();

    if (!dark) {
        style = "QWidget#BalloonProcess {     background-color: "
                "rgba(0, 0, 0, 10); border-radius: 5px; }";
    } else {
        style = "QWidget#BalloonProcess {     background-color: rgba(255, "
                "255, 255, 16); border-radius: 5px; }";
    }
    if (style != styleSheet())
        setStyleSheet(style);
}

void BalloonProcess::onCancelClicked()
{
    m_cancelButton->setEnabled(false);
    m_cancelButton->setText("Cancelling...");
    // FIXME
    // ExportManager::sharedInstance()->cancel(m_item->jobId);
}

void BalloonProcess::updateUI()
{
    QString statusText;
    if (m_item->status == ProcessStatus::Running) {
        statusText = m_item->currentFile.isEmpty() ? "Processing..."
                                                   : m_item->currentFile;
    } else if (m_item->status == ProcessStatus::Completed) {
        statusText = "Completed successfully";
    } else if (m_item->status == ProcessStatus::Failed) {
        statusText = "Failed";
    } else if (m_item->status == ProcessStatus::Cancelled) {
        statusText = "Cancelled";
    }
    m_statusLabel->setText(statusText);

    if (m_item->totalBytes > 0 && m_item->transferredBytes > 0) {
        int progress = (m_item->transferredBytes * 100) / m_item->totalBytes;
        m_progressBar->setValue(progress);
    }

    m_currentFileLabel->setText(m_item->currentFile);

    QString statsText = QString("%1 of %2 items")
                            .arg(m_item->completedItems)
                            .arg(m_item->totalItems);
    if (m_item->failedItems > 0) {
        statsText += QString(" • %1 failed").arg(m_item->failedItems);
    }

    if (m_item->status == ProcessStatus::Running &&
        m_item->transferredBytes > 0) {
        QDateTime now = QDateTime::currentDateTime();
        qint64 elapsed = m_lastUpdateTime.msecsTo(now);
        if (elapsed > 0) {
            qint64 bytesDiff =
                m_item->transferredBytes - m_lastBytesTransferred;
            qint64 bytesPerSecond = (bytesDiff * 1000) / elapsed;
            if (bytesPerSecond > 0) {
                statsText += " • " + iDescriptor::Utils::formatTransferRate(
                                         bytesPerSecond);
            }
            m_lastBytesTransferred = m_item->transferredBytes;
            m_lastUpdateTime = now;
        }
    }
    m_statsLabel->setText(statsText);

    if (m_item->status == ProcessStatus::Running) {
        m_cancelButton->setVisible(true);
        m_actionButton->setVisible(false);
    } else {
        m_cancelButton->setVisible(false);
        if (m_item->type == ProcessType::Export &&
            m_item->status == ProcessStatus::Completed) {
            m_actionButton->setVisible(true);
        }
    }
}

void BalloonProcess::onOpenFolderClicked()
{
    if (!m_item->destinationPath.isEmpty() &&
        m_item->type == ProcessType::Export) {
        QDesktopServices::openUrl(QUrl::fromLocalFile(m_item->destinationPath));
    }
}

void BalloonProcess::enterEvent(QEnterEvent *event)
{
    QWidget::enterEvent(event);
    if (m_item->status == ProcessStatus::Completed ||
        m_item->status == ProcessStatus::Failed ||
        m_item->status == ProcessStatus::Cancelled) {
        if (auto *eff = qobject_cast<QGraphicsOpacityEffect *>(
                m_removeBtn->graphicsEffect())) {
            eff->setOpacity(1.0);
        }
        m_removeBtn->setEnabled(true);
    }
}

void BalloonProcess::leaveEvent(QEvent *event)
{
    QWidget::leaveEvent(event);
    if (auto *eff = qobject_cast<QGraphicsOpacityEffect *>(
            m_removeBtn->graphicsEffect())) {
        eff->setOpacity(0.0);
    }
}

StatusBalloon *StatusBalloon::sharedInstance()
{
    static StatusBalloon instance;
    return &instance;
}

StatusBalloon::StatusBalloon(QWidget *parent) : QBalloonTip(parent)
{
    setMinimumHeight(300);
    setMinimumWidth(300);

    auto *outerLayout = new QVBoxLayout(this);
    outerLayout->setContentsMargins(0, 0, 0, 0);
    outerLayout->setSpacing(0);

    QWidget *container = new QWidget;
#ifndef WIN32
    container->setObjectName("StatusBalloon");
    container->setStyleSheet(QString("QWidget#StatusBalloon { "
                                     "  background-color: %1;"
                                     "  border-radius: 8px;"
                                     "border: 1px solid #ccc;"
                                     "}")
                                 .arg(QApplication::palette()
                                          .color(QPalette::Window)
                                          .name(QColor::HexArgb)));
#endif
    outerLayout->addWidget(container);

    m_mainLayout = new QVBoxLayout(container);
    m_mainLayout->setSpacing(8);
    m_mainLayout->setContentsMargins(15, 15, 15, 15);

    m_noProcesesLabel =
        new QLabel("Export & Import processes will appear here", this);
    m_noProcesesLabel->setAlignment(Qt::AlignCenter);
    m_noProcesesLabel->setWordWrap(true);

    // Header label
    m_headerLabel = new QLabel("Processes");
    m_headerLabel->hide();
    QFont headerFont = m_headerLabel->font();
    headerFont.setPointSize(headerFont.pointSize() + 2);
    headerFont.setBold(true);
    m_headerLabel->setFont(headerFont);
    m_mainLayout->addWidget(m_headerLabel);

    // Container for processes
    m_processesContainer = new QWidget();
    m_processesLayout = new QVBoxLayout(m_processesContainer);

    QScrollArea *scrollArea = new QScrollArea();
    scrollArea->setWidget(m_processesContainer);
    scrollArea->setWidgetResizable(true);
    scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scrollArea->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    scrollArea->setStyleSheet(
        "QScrollArea { background: transparent; border: none; }");
    scrollArea->viewport()->setStyleSheet("background: transparent;");

    m_processesLayout->setSpacing(12);
    m_processesLayout->setContentsMargins(10, 10, 10, 10);
    m_mainLayout->addWidget(scrollArea);

    setLayout(m_mainLayout);
    connect(m_button, &ZIconWidget::clicked, this, &StatusBalloon::handleShow);
    connectExportThreadSignals();
}

void StatusBalloon::connectExportThreadSignals()
{
    auto *ioManager = AppContext::sharedInstance()->ioManager;

    connect(ioManager, &CXX::IOManager::export_job_finished, this,
            &StatusBalloon::onExportJobFinished);

    connect(ioManager, &CXX::IOManager::export_item_finished, this,
            &StatusBalloon::onItemExported);

    connect(ioManager, &CXX::IOManager::import_job_finished, this,
            &StatusBalloon::onImportJobFinished);

    connect(ioManager, &CXX::IOManager::import_item_finished, this,
            &StatusBalloon::onItemImported);

    connect(ioManager, &CXX::IOManager::file_transfer_progress, this,
            &StatusBalloon::onFileTransferProgress);
    // QTimer::singleShot(3000, this, [this]() {
    //     // test
    //     startProcess("Test Export Process", 10, "/path/to/destination",
    //                  ProcessType::Export, QUuid());
    // });
}

void StatusBalloon::onFileTransferProgress(const QUuid &processId,
                                           const QString &currentFile,
                                           qint64 bytesTransferred,
                                           qint64 totalBytes)
{
    QMutexLocker locker(&m_processesMutex);
    if (!m_processes.contains(processId))
        return;

    auto item = m_processes[processId];
    item->currentFile = currentFile;
    item->transferredBytes = bytesTransferred;
    item->totalBytes = totalBytes;

    handleJobUpdate(item);
}

void StatusBalloon::onExportJobFinished(const QUuid &job_id, bool cancelled,
                                        qint64 successful_items,
                                        qint64 failed_items, qint64 total_bytes)
{
    QMutexLocker locker(&m_processesMutex);
    if (!m_processes.contains(job_id)) {
        qDebug() << "Received export job finished signal for unknown job_id:"
                 << job_id;
        return;
    }

    auto item = m_processes[job_id];
    if (cancelled) {
        item->status = ProcessStatus::Cancelled;
    } else {
        if (item->failedItems > 0) {
            item->status = ProcessStatus::Failed;
        } else {
            item->status = ProcessStatus::Completed;
        }
    }
    item->endTime = QDateTime::currentDateTime();

    handleJobUpdate(item);
    updateHeader();
}

void StatusBalloon::onItemExported(const QUuid &job_id,
                                   const QString &file_name,
                                   const QString &destination_path,
                                   bool success, int bytes_transferred,
                                   const QString &error_message)
{
    QMutexLocker locker(&m_processesMutex);
    if (!m_processes.contains(job_id)) {
        qDebug() << "Received export item finished signal for unknown job_id:"
                 << job_id;
        return;
    }

    auto item = m_processes[job_id];
    if (success)
        item->completedItems++;
    else
        item->failedItems++;

    handleJobUpdate(item);
    updateHeader();
}

void StatusBalloon::onImportJobFinished(const QUuid &job_id, bool cancelled,
                                        qint64 successful_items,
                                        qint64 failed_items, qint64 total_bytes)
{
    QMutexLocker locker(&m_processesMutex);
    if (!m_processes.contains(job_id)) {
        qDebug() << "Received import job finished signal for unknown job_id:"
                 << job_id;
        return;
    }

    auto item = m_processes[job_id];
    if (cancelled) {
        item->status = ProcessStatus::Cancelled;
    } else {
        if (item->failedItems > 0) {
            item->status = ProcessStatus::Failed;
        } else {
            item->status = ProcessStatus::Completed;
        }
    }
    item->endTime = QDateTime::currentDateTime();

    handleJobUpdate(item);
    updateHeader();
}

void StatusBalloon::onItemImported(const QUuid &job_id,
                                   const QString &file_name,
                                   const QString &destination_path,
                                   bool success, int bytes_transferred,
                                   const QString &error_message)
{
    QMutexLocker locker(&m_processesMutex);
    if (!m_processes.contains(job_id))
        return;

    auto item = m_processes[job_id];
    if (success)
        item->completedItems++;
    else
        item->failedItems++;

    handleJobUpdate(item);
    updateHeader();
}

QUuid StatusBalloon::startProcess(const QString &title, int totalItems,
                                  const QString &destinationPath,
                                  ProcessType type, const QUuid &jobId)
{
    handleShow(true);

    auto item = std::make_shared<ProcessItem>();
    item->processId = jobId;
    item->type = type;
    item->status = ProcessStatus::Running;
    item->title = title;
    item->totalItems = totalItems;
    item->startTime = QDateTime::currentDateTime();
    item->destinationPath = destinationPath;

    {
        QMutexLocker locker(&m_processesMutex);
        m_processes[jobId] = item;
    }

    createProcessWidget(item);
    updateHeader();

    if (m_button)
        m_button->setIndicatorVisible(true);
    return item->processId;
}

void StatusBalloon::createProcessWidget(std::shared_ptr<ProcessItem> item)
{
    // Pass shared_ptr to widget
    BalloonProcess *processWidget = new BalloonProcess(item);
    item->processWidget = processWidget;
    m_processesLayout->addWidget(processWidget);
    m_processesLayout->addStretch();
}

void StatusBalloon::updateHeader()
{
    // QMutexLocker locker(&m_processesMutex);

    int running = 0, completed = 0, failed = 0, canceled = 0;
    for (const auto &item : m_processes) {
        if (item->status == ProcessStatus::Running)
            running++;
        else if (item->status == ProcessStatus::Completed)
            completed++;
        else if (item->status == ProcessStatus::Failed)
            failed++;
        else if (item->status == ProcessStatus::Cancelled)
            canceled++;
    }
    int total = running + completed + failed + canceled;

    QString headerText = QString("Processes: %1 running").arg(running);
    if (completed > 0 || failed > 0 || canceled > 0) {
        headerText += QString(" • %1 completed").arg(completed);
        if (failed > 0) {
            headerText += QString(" • %1 failed").arg(failed);
        }
        if (canceled > 0) {
            headerText += QString(" • %1 cancelled").arg(canceled);
        }
    }
    m_headerLabel->setText(headerText);

    if (total == 0) {
        m_headerLabel->hide();
        m_noProcesesLabel->show();
        return;
    } else {
        m_headerLabel->show();
        m_noProcesesLabel->hide();
    }
}

void StatusBalloon::handleShow(bool forceVisible)
{
    QPoint pos = m_button->mapToGlobal(
        QPoint(m_button->width() / 2, m_button->height()));

    toggleBaloon(pos, -1, forceVisible);
}

bool StatusBalloon::hasActiveProcesses() const
{
    QMutexLocker locker(&m_processesMutex);
    for (const auto &item : m_processes) {
        if (item->status == ProcessStatus::Running) {
            return true;
        }
    }
    return false;
}

void StatusBalloon::removeProcess(const QUuid &processId)
{
    std::shared_ptr<ProcessItem> item;
    {
        QMutexLocker locker(&m_processesMutex);
        if (!m_processes.contains(processId))
            return;

        item = m_processes[processId];
        m_processes.remove(processId);
    }

    if (item->processWidget) {
        m_processesLayout->removeWidget(item->processWidget);
        item->processWidget->deleteLater();
        item->processWidget = nullptr;
    }

    // hide dot if no active processes left
    if (m_button && !hasActiveProcesses())
        m_button->setIndicatorVisible(false);

    updateHeader();
}

void StatusBalloon::handleJobUpdate(const std::shared_ptr<ProcessItem> &item)
{
    if (item->processWidget) {
        item->processWidget->updateUI();
    }
}

#ifdef WIN32
void StatusBalloon::showEvent(QShowEvent *event)
{
    QBalloonTip::showEvent(event);
    // HWND changes after hide/show, have to reapply acrylic here
    enableMica((HWND)winId());
    SetCorner((HWND)winId(), CornerPreference::Corner_Round);
}
#endif

void StatusBalloon::resizeEvent(QResizeEvent *event)
{
    QBalloonTip::resizeEvent(event);

    if (!m_noProcesesLabel)
        return;

    const int margin = 10;
    int maxWidth = qMax(0, width() - 2 * margin);
    m_noProcesesLabel->setMaximumWidth(maxWidth);
    m_noProcesesLabel->adjustSize();

    int x = (width() - m_noProcesesLabel->width()) / 2;
    int y = (height() - m_noProcesesLabel->height()) / 2;
    x = qMax(margin, x);
    y = qMax(margin, y);

    m_noProcesesLabel->move(x, y);
}
