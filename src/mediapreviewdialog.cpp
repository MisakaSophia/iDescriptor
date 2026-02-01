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

#include "mediapreviewdialog.h"
#include "appcontext.h"
#include "iDescriptor-ui.h"
#include "imageloader.h"
#include "mediastreamermanager.h"
#include "photomodel.h"
#include <QApplication>
#include <QAudioOutput>
#include <QCoreApplication>
#include <QDebug>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QGraphicsPixmapItem>
#include <QGraphicsScene>
#include <QGraphicsView>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QMediaPlayer>
#include <QPushButton>
#include <QResizeEvent>
#include <QScreen>
#include <QSlider>
#include <QTimer>
#include <QVBoxLayout>
#include <QVideoWidget>
#include <QWheelEvent>
#include <QtConcurrent/QtConcurrent>
#include <QtGlobal>

MediaPreviewDialog::MediaPreviewDialog(iDescriptorDevice *device,
                                       AfcClientHandle *afcClient,
                                       const QString &filePath, QWidget *parent)
    : QDialog(parent), m_device(device), m_filePath(filePath),
      m_isVideo(isVideoFile(filePath)), m_mainLayout(nullptr),
      m_controlsLayout(nullptr), m_imageView(nullptr), m_imageScene(nullptr),
      m_pixmapItem(nullptr), m_videoWidget(nullptr), m_mediaPlayer(nullptr),
      m_videoControlsLayout(nullptr), m_playPauseBtn(nullptr),
      m_stopBtn(nullptr), m_repeatBtn(nullptr), m_timelineSlider(nullptr),
      m_timeLabel(nullptr), m_volumeSlider(nullptr), m_volumeLabel(nullptr),
      m_progressTimer(nullptr), m_loadingLabel(nullptr), m_statusLabel(nullptr),
      m_zoomInBtn(nullptr), m_zoomOutBtn(nullptr), m_zoomResetBtn(nullptr),
      m_fitToWindowBtn(nullptr), m_zoomFactor(1.0), m_isRepeatEnabled(true),
      m_isDraggingTimeline(false), m_videoDuration(0), m_afcClient(afcClient)
{
    setWindowTitle(QFileInfo(filePath).fileName() + " - iDescriptor");

    // Make dialog fullscreen
    setWindowState(Qt::WindowMaximized);
    setWindowFlags(Qt::Window | Qt::WindowMaximizeButtonHint |
                   Qt::WindowCloseButtonHint);

    // Use full screen size
    const QSize screenSize = QApplication::primaryScreen()->size();
    resize(screenSize);

    setupUI();
    loadMedia();
    connect(AppContext::sharedInstance(), &AppContext::deviceRemoved, this,
            [this](const std::string &udid) {
                if (udid == m_device->udid) {
                    close();
                }
            });
}

MediaPreviewDialog::~MediaPreviewDialog()
{
    // Release the streamer if it was used for video
    if (m_isVideo) {
        MediaStreamerManager::sharedInstance()->releaseStreamer(m_filePath);
    }
}

void MediaPreviewDialog::setupUI()
{
    m_mainLayout = new QVBoxLayout(this);
    m_mainLayout->setContentsMargins(0, 0, 0, 0);
    m_mainLayout->setSpacing(0);

    // Loading label
    m_loadingLabel = new QLabel("Loading...", this);
    m_loadingLabel->setAlignment(Qt::AlignCenter);
    m_loadingLabel->setStyleSheet(
        "QLabel { font-size: 16px; color: #666; padding: 20px; }");
    m_mainLayout->addWidget(m_loadingLabel);

    if (m_isVideo) {
        setupVideoView();
    } else {
        setupImageView();
    }

    // Status bar
    // more margin because of border radius on macOS
    m_statusLabel = new QLabel(this);
#ifdef Q_OS_MAC
    m_statusLabel->setStyleSheet("QLabel { margin-left: 15px; }");
#else
    m_statusLabel->setStyleSheet("QLabel { margin-left: 5px; }");
#endif
    m_mainLayout->addWidget(m_statusLabel);
}

void MediaPreviewDialog::setupImageView()
{
    // Graphics view for image display with zoom/pan
    m_imageScene = new QGraphicsScene(this);
    m_imageView = new QGraphicsView(m_imageScene, this);
    m_imageView->setDragMode(QGraphicsView::ScrollHandDrag);
    m_imageView->setRenderHint(QPainter::Antialiasing);
    m_imageView->setVisible(false);
    m_mainLayout->addWidget(m_imageView);

    // Controls layout
    m_controlsLayout = new QHBoxLayout();
    m_controlsLayout->setContentsMargins(10, 5, 10, 5);

    m_zoomInBtn = new QPushButton("Zoom In", this);
    m_zoomOutBtn = new QPushButton("Zoom Out", this);
    m_zoomResetBtn = new QPushButton("100%", this);
    m_fitToWindowBtn = new QPushButton("Fit to Window", this);

    m_controlsLayout->addWidget(m_zoomInBtn);
    m_controlsLayout->addWidget(m_zoomOutBtn);
    m_controlsLayout->addWidget(m_zoomResetBtn);
    m_controlsLayout->addWidget(m_fitToWindowBtn);
    m_controlsLayout->addStretch();

    m_mainLayout->addLayout(m_controlsLayout);

    // Connect signals
    connect(m_zoomInBtn, &QPushButton::clicked, this,
            &MediaPreviewDialog::zoomIn);
    connect(m_zoomOutBtn, &QPushButton::clicked, this,
            &MediaPreviewDialog::zoomOut);
    connect(m_zoomResetBtn, &QPushButton::clicked, this,
            &MediaPreviewDialog::zoomReset);
    connect(m_fitToWindowBtn, &QPushButton::clicked, this,
            &MediaPreviewDialog::fitToWindow);
}

void MediaPreviewDialog::setupVideoView()
{
    // Video widget
    m_videoWidget = new QVideoWidget(this);
    m_videoWidget->setVisible(false);
    m_videoWidget->setSizePolicy(QSizePolicy::Expanding,
                                 QSizePolicy::Expanding);
    m_mainLayout->addWidget(m_videoWidget, 1); // Give it stretch factor 1

    // Media player
    m_mediaPlayer = new QMediaPlayer(this);
    m_mediaPlayer->setVideoOutput(m_videoWidget);

    // Set up audio output explicitly
    auto *audioOutput = new QAudioOutput(this);
    audioOutput->setVolume(1.0); // Full volume
    m_mediaPlayer->setAudioOutput(audioOutput);

    // Setup video controls
    setupVideoControls();

    // Connect media player signals
    connect(m_mediaPlayer, &QMediaPlayer::durationChanged, this,
            &MediaPreviewDialog::onMediaPlayerDurationChanged);
    connect(m_mediaPlayer, &QMediaPlayer::positionChanged, this,
            &MediaPreviewDialog::onMediaPlayerPositionChanged);
    connect(m_mediaPlayer, &QMediaPlayer::playbackStateChanged, this,
            &MediaPreviewDialog::onMediaPlayerStateChanged);
    connect(m_mediaPlayer, &QMediaPlayer::errorOccurred, this,
            [this](QMediaPlayer::Error error, const QString &errorString) {
                qDebug() << "MediaPlayer Error:" << error << errorString;
                m_statusLabel->setText("Error: " + errorString);
                m_loadingLabel->setText("Error: " + errorString);
                m_loadingLabel->show();
                m_videoWidget->hide();
            });
    // Setup progress timer for smooth updates
    m_progressTimer = new QTimer(this);
    connect(m_progressTimer, &QTimer::timeout, this,
            &MediaPreviewDialog::updateVideoProgress);
}

void MediaPreviewDialog::loadMedia()
{
    if (m_isVideo) {
        return loadVideo();
    }
    loadImage();
}

void MediaPreviewDialog::loadImage()
{
    auto future = QtConcurrent::run(
        [this]() { return ImageLoader::loadImage(m_device, m_filePath); });

    auto *watcher = new QFutureWatcher<QPixmap>(this);
    connect(watcher, &QFutureWatcher<QPixmap>::finished, this,
            [this, watcher]() {
                QPixmap pixmap = watcher->result();
                if (!pixmap.isNull()) {
                    m_originalPixmap = pixmap;
                    onImageLoaded();
                } else {
                    onImageLoadFailed();
                }
                watcher->deleteLater();
            });
    watcher->setFuture(future);
}

void MediaPreviewDialog::loadVideo()
{
    m_videoWidget->setVisible(true);

    // Get streamer URL from the singleton manager
    QUrl streamUrl = MediaStreamerManager::sharedInstance()->getStreamUrl(
        m_device, m_afcClient, m_filePath);
    qDebug() << "Streaming video from URL:" << streamUrl;
    if (streamUrl.isEmpty()) {
        m_statusLabel->setText("Failed to start video stream");
        return;
    }

    m_mediaPlayer->setSource(streamUrl);
    m_mediaPlayer->play();
    m_loadingLabel->hide();
    m_statusLabel->setText(
        QString("Playing: %1").arg(QFileInfo(m_filePath).fileName()));
}

void MediaPreviewDialog::onImageLoaded()
{
    m_loadingLabel->hide();
    m_imageView->setVisible(true);

    // Add pixmap to scene
    m_pixmapItem = m_imageScene->addPixmap(m_originalPixmap);
    m_imageScene->setSceneRect(m_originalPixmap.rect());

    // Fit to window initially
    fitToWindow();

    // Update status
    m_statusLabel->setText(QString("Image: %1 (%2x%3)")
                               .arg(QFileInfo(m_filePath).fileName())
                               .arg(m_originalPixmap.width())
                               .arg(m_originalPixmap.height()));
}

void MediaPreviewDialog::onImageLoadFailed()
{
    m_loadingLabel->setText("Failed to load image");
    m_statusLabel->setText("Error loading image");
}

void MediaPreviewDialog::wheelEvent(QWheelEvent *event)
{
    if (!m_isVideo && m_imageView && m_imageView->isVisible()) {
        if (event->modifiers() & Qt::ControlModifier) {
            // Zoom with Ctrl + Mouse Wheel
            const double scaleFactor = 1.15;
            if (event->angleDelta().y() > 0) {
                zoom(scaleFactor);
            } else {
                zoom(1.0 / scaleFactor);
            }
            event->accept();
            return;
        }
    }
    QDialog::wheelEvent(event);
}

void MediaPreviewDialog::keyPressEvent(QKeyEvent *event)
{
    // Image shortcuts
    if (!m_isVideo && m_imageView) {
        switch (event->key()) {
        case Qt::Key_Plus:
        case Qt::Key_Equal:
            zoomIn();
            event->accept();
            return;
        case Qt::Key_Minus:
            zoomOut();
            event->accept();
            return;
        case Qt::Key_0:
            zoomReset();
            event->accept();
            return;
        case Qt::Key_F:
            fitToWindow();
            event->accept();
            return;
        }
    }

    // Video shortcuts
    if (m_isVideo && m_mediaPlayer) {
        switch (event->key()) {
        case Qt::Key_Space:
            onPlayPauseClicked();
            event->accept();
            return;
        case Qt::Key_S:
            onStopClicked();
            event->accept();
            return;
        case Qt::Key_R:
            m_repeatBtn->toggle();
            event->accept();
            return;
        case Qt::Key_Left:
            // Seek backward 10 seconds
            if (m_videoDuration > 0) {
                qint64 newPos = qMax(0LL, m_mediaPlayer->position() - 10000);
                m_mediaPlayer->setPosition(newPos);
            }
            event->accept();
            return;
        case Qt::Key_Right:
            // Seek forward 10 seconds
            if (m_videoDuration > 0) {
                qint64 newPos =
                    qMin(m_videoDuration, m_mediaPlayer->position() + 10000);
                m_mediaPlayer->setPosition(newPos);
            }
            event->accept();
            return;
        }
    }

    // Global shortcuts
    if (event->key() == Qt::Key_Escape) {
        close();
        event->accept();
        return;
    }

    QDialog::keyPressEvent(event);
}

void MediaPreviewDialog::resizeEvent(QResizeEvent *event)
{
    QDialog::resizeEvent(event);

    // Auto-fit when window is resized if we're close to fit-to-window size
    if (!m_isVideo && m_imageView && m_imageView->isVisible() &&
        !m_originalPixmap.isNull()) {
        const QSize viewSize = m_imageView->viewport()->size();
        const QSize pixmapSize = m_originalPixmap.size();
        const double fitScale =
            qMin(static_cast<double>(viewSize.width()) / pixmapSize.width(),
                 static_cast<double>(viewSize.height()) / pixmapSize.height());

        // If current zoom is close to fit-to-window, re-fit
        if (qAbs(m_zoomFactor - fitScale) < 0.1) {
            fitToWindow();
        }
    }
}

void MediaPreviewDialog::zoomIn() { zoom(1.25); }

void MediaPreviewDialog::zoomOut() { zoom(1.0 / 1.25); }

void MediaPreviewDialog::zoomReset()
{
    if (m_imageView && m_originalPixmap.isNull() == false) {
        m_imageView->resetTransform();
        m_zoomFactor = 1.0;
        updateZoomStatus();
    }
}

void MediaPreviewDialog::fitToWindow()
{
    if (!m_imageView || m_originalPixmap.isNull())
        return;

    const QSize viewSize = m_imageView->viewport()->size();
    const QSize pixmapSize = m_originalPixmap.size();

    const double scaleX =
        static_cast<double>(viewSize.width()) / pixmapSize.width();
    const double scaleY =
        static_cast<double>(viewSize.height()) / pixmapSize.height();
    const double scale = qMin(scaleX, scaleY);

    m_imageView->resetTransform();
    m_imageView->scale(scale, scale);
    m_zoomFactor = scale;
    updateZoomStatus();
}

void MediaPreviewDialog::zoom(double factor)
{
    if (!m_imageView)
        return;

    m_imageView->scale(factor, factor);
    m_zoomFactor *= factor;
    updateZoomStatus();
}

// void MediaPreviewDialog::updateZoomStatus()
// {
//     if (!m_isVideo && !m_originalPixmap.isNull()) {
//         m_statusLabel->setText(QString("Image: %1 (%2x%3) - Zoom: %4%")
//                                    .arg(QFileInfo(m_filePath).fileName())
//                                    .arg(m_originalPixmap.width())
//                                    .arg(m_originalPixmap.height())
//                                    .arg(qRound(m_zoomFactor * 100)));
//     }
// }

void MediaPreviewDialog::updateZoomStatus()
{
    if (!m_isVideo && !m_originalPixmap.isNull()) {
        m_statusLabel->setText(QString("Image: %1 (%2x%3) - Zoom: %4%")
                                   .arg(QFileInfo(m_filePath).fileName())
                                   .arg(m_originalPixmap.width())
                                   .arg(m_originalPixmap.height())
                                   .arg(qRound(m_zoomFactor * 100)));
    }
}

bool MediaPreviewDialog::isVideoFile(const QString &filePath) const
{
    const QString lower = filePath.toLower();
    return lower.endsWith(".mov") || lower.endsWith(".mp4") ||
           lower.endsWith(".avi") || lower.endsWith(".m4v");
}

void MediaPreviewDialog::setupVideoControls()
{
    // Create video controls layout
    m_videoControlsLayout = new QHBoxLayout();
    m_videoControlsLayout->setContentsMargins(10, 5, 10, 5);
    m_videoControlsLayout->setSpacing(10);

    // Play/Pause button
    m_playPauseBtn = new QPushButton("⏸️", this);
    m_playPauseBtn->setMaximumWidth(40);
    m_playPauseBtn->setMinimumHeight(30);
    m_playPauseBtn->setToolTip("Play/Pause (Space)");
    m_playPauseBtn->setStyleSheet("QPushButton { font-size: 14px; }");
    connect(m_playPauseBtn, &QPushButton::clicked, this,
            &MediaPreviewDialog::onPlayPauseClicked);

    // Stop button
    m_stopBtn = new QPushButton("⏹️", this);
    m_stopBtn->setMaximumWidth(40);
    m_stopBtn->setMinimumHeight(30);
    m_stopBtn->setToolTip("Stop (S)");
    m_stopBtn->setStyleSheet("QPushButton { font-size: 14px; }");
    connect(m_stopBtn, &QPushButton::clicked, this,
            &MediaPreviewDialog::onStopClicked);

    // Repeat button
    m_repeatBtn = new QPushButton("🔁", this);
    m_repeatBtn->setMaximumWidth(40);
    m_repeatBtn->setMinimumHeight(30);
    m_repeatBtn->setCheckable(true);
    m_repeatBtn->setToolTip("Toggle Repeat (R)");
    m_repeatBtn->setStyleSheet("QPushButton { font-size: 14px; }");
    connect(m_repeatBtn, &QPushButton::toggled, this,
            &MediaPreviewDialog::onRepeatToggled);

    // Timeline slider
    m_timelineSlider = new ZSlider(Qt::Horizontal, this);
    m_timelineSlider->setMinimum(0);
    m_timelineSlider->setMaximum(1000);
    m_timelineSlider->setValue(0);
    m_timelineSlider->setToolTip("Seek timeline");
    connect(m_timelineSlider, &ZSlider::valueChanged, this,
            &MediaPreviewDialog::onTimelineValueChanged);
    connect(m_timelineSlider, &ZSlider::sliderPressed, this,
            &MediaPreviewDialog::onTimelinePressed);
    connect(m_timelineSlider, &ZSlider::sliderReleased, this,
            &MediaPreviewDialog::onTimelineReleased);

    // Time label
    m_timeLabel = new QLabel("00:00 / 00:00", this);
    m_timeLabel->setMinimumWidth(100);
    m_timeLabel->setStyleSheet("QLabel { font-family: monospace; }");

    // Volume slider
    m_volumeSlider = new QSlider(Qt::Horizontal, this);
    m_volumeSlider->setMinimum(0);
    m_volumeSlider->setMaximum(100);
    m_volumeSlider->setValue(100); // Default to full volume
    m_volumeSlider->setMaximumWidth(100);
    m_volumeSlider->setToolTip("Volume");
    connect(m_volumeSlider, &QSlider::valueChanged, this,
            &MediaPreviewDialog::onVolumeChanged);

    // Volume label
    m_volumeLabel = new QLabel("🔊", this);
    m_volumeLabel->setStyleSheet("QLabel { font-size: 14px; }");

    // Add widgets to layout
    m_videoControlsLayout->addWidget(m_playPauseBtn);
    m_videoControlsLayout->addWidget(m_stopBtn);
    m_videoControlsLayout->addWidget(m_repeatBtn);
    m_videoControlsLayout->addWidget(m_timelineSlider, 1); // Stretch factor 1
    m_videoControlsLayout->addWidget(m_timeLabel);
    m_videoControlsLayout->addWidget(m_volumeLabel);
    m_videoControlsLayout->addWidget(m_volumeSlider);

    // Add controls layout to main layout
    m_mainLayout->addLayout(m_videoControlsLayout);
}

void MediaPreviewDialog::onPlayPauseClicked()
{
    if (!m_mediaPlayer)
        return;

    if (m_mediaPlayer->playbackState() == QMediaPlayer::PlayingState) {
        m_mediaPlayer->pause();
    } else {
        m_mediaPlayer->play();
    }
}

void MediaPreviewDialog::onStopClicked()
{
    if (!m_mediaPlayer)
        return;

    m_mediaPlayer->stop();
    if (m_progressTimer) {
        m_progressTimer->stop();
    }
}

void MediaPreviewDialog::onRepeatToggled(bool enabled)
{
    m_isRepeatEnabled = enabled;
    m_repeatBtn->setStyleSheet(
        enabled ? "QPushButton { background-color: #4CAF50; color: white; }"
                : "");

    qDebug() << "Repeat mode:" << (enabled ? "ON" : "OFF");
}

void MediaPreviewDialog::onTimelinePressed()
{
    m_isDraggingTimeline = true;
    if (m_progressTimer) {
        m_progressTimer->stop();
    }
}

void MediaPreviewDialog::onTimelineReleased()
{
    m_isDraggingTimeline = false;
    if (m_mediaPlayer && m_videoDuration > 0) {
        // Seek to the selected position
        qint64 position = (m_timelineSlider->value() * m_videoDuration) / 1000;
        m_mediaPlayer->setPosition(position);
    }

    // Restart progress timer if playing
    if (m_mediaPlayer &&
        m_mediaPlayer->playbackState() == QMediaPlayer::PlayingState) {
        m_progressTimer->start(100); // Update every 100ms
    }
}

void MediaPreviewDialog::onTimelineValueChanged(int value)
{
    if (m_isDraggingTimeline && m_videoDuration > 0) {
        // Update time display while dragging
        qint64 position = (value * m_videoDuration) / 1000;
        updateVideoTimeDisplay();
    }
}

void MediaPreviewDialog::updateVideoProgress()
{
    if (!m_mediaPlayer || m_isDraggingTimeline)
        return;

    qint64 position = m_mediaPlayer->position();
    if (m_videoDuration > 0) {
        int sliderValue = static_cast<int>((position * 1000) / m_videoDuration);
        m_timelineSlider->setValue(sliderValue);
    }

    updateVideoTimeDisplay();
}

void MediaPreviewDialog::onMediaPlayerStateChanged()
{
    if (!m_mediaPlayer)
        return;

    QMediaPlayer::PlaybackState state = m_mediaPlayer->playbackState();

    switch (state) {
    case QMediaPlayer::PlayingState:
        m_playPauseBtn->setText("⏸️");
        m_playPauseBtn->setToolTip("Pause (Space)");
        if (m_progressTimer) {
            m_progressTimer->start(100);
        }
        break;
    case QMediaPlayer::PausedState:
        m_playPauseBtn->setText("▶️");
        m_playPauseBtn->setToolTip("Play (Space)");
        if (m_progressTimer) {
            m_progressTimer->stop();
        }
        break;
    case QMediaPlayer::StoppedState:
        m_playPauseBtn->setText("▶️");
        m_playPauseBtn->setToolTip("Play (Space)");
        if (m_progressTimer) {
            m_progressTimer->stop();
        }
        m_timelineSlider->setValue(0);

        // Handle repeat functionality
        if (m_isRepeatEnabled) {
            QTimer::singleShot(100, this, [this]() {
                if (m_mediaPlayer) {
                    m_mediaPlayer->play();
                }
            });
        }
        break;
    }
}

void MediaPreviewDialog::onMediaPlayerDurationChanged(qint64 duration)
{
    m_videoDuration = duration;
    updateVideoTimeDisplay();

    // Update status with video info
    if (duration > 0) {
        QString durationStr;
        formatTime(duration, durationStr);
        m_statusLabel->setText(QString("Video: %1 - Duration: %2")
                                   .arg(QFileInfo(m_filePath).fileName())
                                   .arg(durationStr));
    }
}

void MediaPreviewDialog::onMediaPlayerPositionChanged(qint64 position)
{
    if (!m_isDraggingTimeline) {
        updateVideoProgress();
    }
}

void MediaPreviewDialog::updateVideoTimeDisplay()
{
    if (!m_mediaPlayer)
        return;

    qint64 currentPos =
        m_isDraggingTimeline
            ? (m_timelineSlider->value() * m_videoDuration) / 1000
            : m_mediaPlayer->position();

    QString currentTimeStr, durationStr;
    formatTime(currentPos, currentTimeStr);
    formatTime(m_videoDuration, durationStr);

    m_timeLabel->setText(QString("%1 / %2").arg(currentTimeStr, durationStr));
}

void MediaPreviewDialog::formatTime(qint64 milliseconds, QString &timeString)
{
    qint64 seconds = milliseconds / 1000;
    qint64 minutes = seconds / 60;
    qint64 hours = minutes / 60;

    seconds %= 60;
    minutes %= 60;

    if (hours > 0) {
        timeString = QString("%1:%2:%3")
                         .arg(hours, 2, 10, QChar('0'))
                         .arg(minutes, 2, 10, QChar('0'))
                         .arg(seconds, 2, 10, QChar('0'));
    } else {
        timeString = QString("%1:%2")
                         .arg(minutes, 2, 10, QChar('0'))
                         .arg(seconds, 2, 10, QChar('0'));
    }
}

void MediaPreviewDialog::onVolumeChanged(int value)
{
    if (!m_mediaPlayer)
        return;

    QAudioOutput *audioOutput = m_mediaPlayer->audioOutput();
    if (audioOutput) {
        float volume = static_cast<float>(value) / 100.0f;
        audioOutput->setVolume(volume);

        // Update volume icon based on level
        if (value == 0) {
            m_volumeLabel->setText("🔇");
        } else if (value < 30) {
            m_volumeLabel->setText("🔈");
        } else if (value < 70) {
            m_volumeLabel->setText("🔉");
        } else {
            m_volumeLabel->setText("🔊");
        }

        qDebug() << "Volume changed to:" << value << "%" << "(" << volume
                 << ")";
    }
}

bool MediaPreviewDialog::event(QEvent *event)
{
    // FIXME: lets implement this on all dialogs
    // catch platform Close (Cmd+W on macOS)
    if (event->type() == QEvent::ShortcutOverride) {
        if (auto *ke = dynamic_cast<QKeyEvent *>(event)) {
            const Qt::KeyboardModifiers mods = ke->modifiers();
            if (ke->key() == Qt::Key_W &&
                (mods & (Qt::MetaModifier | Qt::ControlModifier))) {
                ke->accept();
                close();
                return true;
            }
            if (ke->key() == Qt::Key_Escape) {
                ke->accept();
                close();
                return true;
            }
        }
    }
    return QDialog::event(event);
}