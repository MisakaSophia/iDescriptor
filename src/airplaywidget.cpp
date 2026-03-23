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

#include "airplaywidget.h"
#include <QApplication>
#include <QCheckBox>
#include <QCloseEvent>
#include <QComboBox>
#include <QDebug>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFileInfo>
#include <QFont>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QMediaPlayer>
#include <QMessageBox>
#include <QPalette>
#include <QPixmap>
#include <QProcess>
#include <QPushButton>
#include <QSpinBox>
#include <QStackedWidget>
#include <QVBoxLayout>
#include <QVideoWidget>
#ifdef Q_OS_LINUX
// V4L2 includes
#include <cstring>
#include <errno.h>
#include <fcntl.h>
#include <linux/videodev2.h>
#include <sys/ioctl.h>
#include <unistd.h>
#endif
#include "settingsmanager.h"

#include <uxplay/renderers/video_renderer.h>
#include <uxplay/uxplay.h>

#include "diagnosedialog.h"
#ifdef WIN32
#include "platform/windows/win_common.h"
#endif
#include "toolboxwidget.h"

AirPlaySettings::AirPlaySettings()
    : fps(SettingsManager::sharedInstance()->airplayFps()),
      noHold(SettingsManager::sharedInstance()->airplayNoHold())
{
}

QStringList AirPlaySettings::toArgs() const
{
    QStringList args;

    // FPS
    args << "-fps" << QString::number(fps);

    // Allow new connections to take over
    if (noHold)
        args << "-nohold";

    return args;
}

AirPlaySettingsDialog::AirPlaySettingsDialog(QWidget *parent)
    : QDialog(parent), m_settings(AirPlaySettings())
{
    setupUI();
    setWindowTitle("AirPlay Settings");
    resize(300, 300);
}

void AirPlaySettingsDialog::setupUI()
{
    QVBoxLayout *mainLayout = new QVBoxLayout(this);

    // Video Settings Group
    QGroupBox *videoGroup = new QGroupBox("Video Settings");
    QFormLayout *videoLayout = new QFormLayout(videoGroup);

    // FPS Layout
    QVBoxLayout *fpsLayout = new QVBoxLayout();
    m_fpsComboBox = new QComboBox();
    m_fpsComboBox->addItems({"24", "30", "60", "120"});
    m_fpsComboBox->setCurrentText(
        QString::number(SettingsManager::sharedInstance()->airplayFps()));
    m_fpsComboBox->setToolTip("Set maximum allowed streaming framerate");

    QLabel *fpsFootnote =
        new QLabel("Note: Older devices may not support higher framerates. If "
                   "you are experiencing issues, set this to 30 FPS or lower.");
    fpsFootnote->setWordWrap(true);
    fpsFootnote->setStyleSheet("color: #666; font-size: 12px;");
    fpsLayout->addWidget(m_fpsComboBox);
    fpsLayout->addWidget(fpsFootnote);

    videoLayout->addRow("Max FPS:", fpsLayout);

    m_noHoldCheckbox = new QCheckBox("Allow New Connections to Take Over");
    m_noHoldCheckbox->setChecked(
        SettingsManager::sharedInstance()->airplayNoHold());
    videoLayout->addRow(m_noHoldCheckbox);

    mainLayout->addWidget(videoGroup);
    // Buttons
    QDialogButtonBox *buttonBox =
        new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    connect(buttonBox, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);
    mainLayout->addWidget(buttonBox);
}

AirPlaySettings AirPlaySettingsDialog::getSettings() const
{
    AirPlaySettings settings;
    settings.fps = m_fpsComboBox->currentText().toInt();
    settings.noHold = m_noHoldCheckbox->isChecked();
    return settings;
}

AirPlayWidget::AirPlayWidget(QWidget *parent)
    : Tool(parent, false), m_stackedWidget(nullptr), m_tutorialWidget(nullptr),
      m_streamingWidget(nullptr), m_loadingIndicator(nullptr),
      m_loadingLabel(nullptr), m_tutorialPlayer(nullptr),
      m_tutorialVideoWidget(nullptr), m_videoLabel(nullptr),
      m_tutorialLayout(nullptr), m_settingsButton(nullptr),
#ifdef __linux__
      m_v4l2Checkbox(nullptr), m_v4l2_fd(-1), m_v4l2_width(0), m_v4l2_height(0),
      m_v4l2_enabled(false),
#endif
      m_serverThread(nullptr), m_serverRunning(false), m_clientConnected(false)
{
    setupUI();
    setMinimumSize(800, 600);
    QTimer::singleShot(0, this, [this]() {
        /* HACK: qt ignores resize() calls so let's workaround */
        setMinimumSize(0, 0);
    });

/* FIXME: this can be handled better, add linux support */
#ifdef WIN32
    bool bonjour = IsBonjourServiceInstalled();
    if (!bonjour) {
        QMessageBox::warning(
            this, "Bonjour Service Not Installed",
            "Bonjour service is not installed on your system. Please install "
            "it to enable AirPlay functionality.");

        DiagnoseDialog *diagnoseDialog = new DiagnoseDialog();
        diagnoseDialog->show();
        QTimer::singleShot(0, this, &AirPlayWidget::close);
        return;
    }
#endif
    QTimer::singleShot(500, this, &AirPlayWidget::startAirPlayServer);
}

AirPlayWidget::~AirPlayWidget()
{
    stopAirPlayServer();
#ifdef Q_OS_LINUX
    closeV4L2();
#endif
}

void AirPlayWidget::setupUI()
{
    setWindowTitle("AirPlay - iDescriptor");
    QVBoxLayout *mainLayout = new QVBoxLayout(this);
    m_stackedWidget = new QStackedWidget(this);

    m_tutorialWidget = new QWidget();
    m_tutorialLayout = new QVBoxLayout(m_tutorialWidget);
    m_tutorialLayout->setContentsMargins(0, 0, 0, 0);
    m_tutorialLayout->setSpacing(20);

    m_loadingIndicator = new QProcessIndicator();
    m_loadingIndicator->setType(QProcessIndicator::line_rotate);
    m_loadingIndicator->setFixedSize(24, 24);
    m_loadingIndicator->start();

    QHBoxLayout *loadingLayout = new QHBoxLayout();
    m_loadingLabel = new QLabel("Starting AirPlay Server...");
    loadingLayout->setContentsMargins(0, 40, 0, 0);
    loadingLayout->addStretch();
    loadingLayout->addWidget(m_loadingLabel);
    loadingLayout->addSpacing(5);
    loadingLayout->addWidget(m_loadingIndicator);
    loadingLayout->addStretch();

    m_tutorialLayout->addLayout(loadingLayout);
    m_tutorialLayout->addSpacing(1);

    // Settings button (shown when no client connected)
    m_settingsButton = new QPushButton("Settings");
    m_settingsButton->setVisible(false);
    connect(m_settingsButton, &QPushButton::clicked, this,
            &AirPlayWidget::showSettingsDialog);
    QHBoxLayout *settingsLayout = new QHBoxLayout();
    settingsLayout->addStretch();
    settingsLayout->addWidget(m_settingsButton);
    settingsLayout->addStretch();
    m_tutorialLayout->addLayout(settingsLayout);

    QTimer::singleShot(100, this, &AirPlayWidget::setupTutorialVideo);

    m_streamingWidget = new QWidget();
    QVBoxLayout *streamingLayout = new QVBoxLayout(m_streamingWidget);
    streamingLayout->setContentsMargins(10, 10, 10, 10);
    streamingLayout->setSpacing(10);

#ifdef __linux__
    // Add V4L2 checkbox at the top of streaming view
    setupV4L2Checkbox();
    if (m_v4l2Checkbox) {
        streamingLayout->addWidget(m_v4l2Checkbox);
    }
#endif

    // Video display
    m_videoLabel = new QLabel();
    m_videoLabel->setAlignment(Qt::AlignCenter);
    m_videoLabel->setScaledContents(false);
    streamingLayout->addWidget(m_videoLabel, 1);

    // Add all widgets to stacked widget
    m_stackedWidget->addWidget(m_tutorialWidget);
    m_stackedWidget->addWidget(m_streamingWidget);

    // Start with tutorial widget
    m_stackedWidget->setCurrentWidget(m_tutorialWidget);
    mainLayout->addWidget(m_stackedWidget);
#ifdef __linux__
    m_v4l2_enabled = false; // Disable V4L2 by default
#endif
}

void AirPlayWidget::setupTutorialVideo()
{
    m_tutorialPlayer = new QMediaPlayer(this);
    m_tutorialVideoWidget = new QVideoWidget();
    m_tutorialVideoWidget->setSizePolicy(QSizePolicy::Expanding,
                                         QSizePolicy::Expanding);

    m_tutorialPlayer->setVideoOutput(m_tutorialVideoWidget);
    m_tutorialPlayer->setSource(QUrl("qrc:/resources/airplay-tutorial.mp4"));
    m_tutorialVideoWidget->setAspectRatioMode(
        Qt::AspectRatioMode::KeepAspectRatioByExpanding);
    m_tutorialVideoWidget->setStyleSheet(
        "QVideoWidget { background-color: transparent; }");
    // Loop the tutorial video
    connect(m_tutorialPlayer, &QMediaPlayer::mediaStatusChanged, this,
            [this](QMediaPlayer::MediaStatus status) {
                if (status == QMediaPlayer::EndOfMedia) {
                    m_tutorialPlayer->setPosition(0);
                    m_tutorialPlayer->play();
                }
            });

    // Auto-play when ready
    connect(m_tutorialPlayer, &QMediaPlayer::mediaStatusChanged, this,
            [this](QMediaPlayer::MediaStatus status) {
                if (status == QMediaPlayer::LoadedMedia) {
                    m_tutorialPlayer->play();
                }
            });
    m_tutorialVideoWidget->setVisible(false);
    m_tutorialLayout->addWidget(m_tutorialVideoWidget, 1);
}

void AirPlayWidget::showTutorialView()
{
    m_stackedWidget->setCurrentWidget(m_tutorialWidget);
    if (m_tutorialPlayer) {
        m_tutorialPlayer->play();
        m_loadingIndicator->start();
    }
}

void AirPlayWidget::showStreamingView()
{
    m_loadingIndicator->stop();
    m_stackedWidget->setCurrentWidget(m_streamingWidget);
    if (m_tutorialPlayer) {
        m_tutorialPlayer->pause();
    }
}

void AirPlayWidget::showSettingsDialog()
{
    AirPlaySettingsDialog dialog(this);
    if (dialog.exec() == QDialog::Accepted) {
        AirPlaySettings newSettings = dialog.getSettings();

        // Save settings
        SettingsManager::sharedInstance()->setAirplayFps(newSettings.fps);
        SettingsManager::sharedInstance()->setAirplayNoHold(newSettings.noHold);

        QMessageBox::information(this, "Settings Saved",
                                 "AirPlay will be restarted to apply the new "
                                 "settings.");
        ToolboxWidget::sharedInstance()->restartAirPlayWidget();
    }
}

void AirPlayWidget::startAirPlayServer()
{
    if (m_serverRunning)
        return;

    m_serverThread = new AirPlayServerThread(this);
    connect(m_serverThread, &AirPlayServerThread::statusChanged, this,
            &AirPlayWidget::onServerStatusChanged);
    connect(m_serverThread, &AirPlayServerThread::videoFrameReady, this,
            &AirPlayWidget::updateVideoFrame);
    connect(m_serverThread, &AirPlayServerThread::clientConnectionChanged, this,
            &AirPlayWidget::onClientConnectionChanged);
    connect(m_serverThread, &AirPlayServerThread::errorOccurred, this,
            [this](const QString &message) {
                QMessageBox::critical(this, "AirPlay Server Error", message);
                close();
            });

    QStringList args = m_settings.toArgs();
    m_serverThread->setArguments(args);
    m_serverThread->start();
}

void AirPlayWidget::stopAirPlayServer()
{
    if (m_serverThread) {
        m_serverThread->quit();
        m_serverThread->deleteLater();
        m_serverThread = nullptr;
    }
    m_serverRunning = false;
}

void AirPlayWidget::updateVideoFrame(QByteArray frameData, int width,
                                     int height)
{
    if (frameData.size() != width * height * 3) {
        qDebug() << "Invalid frame data size";
        return;
    }

#ifdef __linux__
    // V4L2 output if enabled
    if (m_v4l2_enabled) {
        writeFrameToV4L2((uint8_t *)frameData.data(), width, height);
        // Show message instead of rendering video when V4L2 is active
        m_videoLabel->setText("Currently being shared via virtual camera");
        return;
    }
#endif

    QImage image((const uchar *)frameData.data(), width, height,
                 QImage::Format_RGB888);
    QPixmap pixmap = QPixmap::fromImage(image);

    // Scale pixmap to fit label while maintaining aspect ratio
    QSize labelSize = m_videoLabel->size();
    QPixmap scaledPixmap =
        pixmap.scaled(labelSize, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    m_videoLabel->setPixmap(scaledPixmap);
}

void AirPlayWidget::onServerStatusChanged(bool running)
{
    m_serverRunning = running;

    if (running) {
        // Server started successfully, hide loading indicator and show tutorial
        // video
        m_loadingLabel->setText("Waiting for device connection");

        // Show tutorial video and instructions
        m_tutorialVideoWidget->setVisible(true);

        // Show settings button when server is running but no client connected
        m_settingsButton->setVisible(!m_clientConnected);

        // Show tutorial video and instructions
        QLabel *instructionLabel = m_tutorialWidget->findChild<QLabel *>();
        if (instructionLabel && !instructionLabel->text().contains("Follow")) {
            // Find the instruction label (not title or loading label)
            QList<QLabel *> labels = m_tutorialWidget->findChildren<QLabel *>();
            for (QLabel *label : labels) {
                if (label->text().contains("Follow")) {
                    label->setVisible(true);
                    break;
                }
            }
        }

        if (m_tutorialPlayer) {
            m_tutorialPlayer->play();
        }
    }
}

void AirPlayWidget::onClientConnectionChanged(bool connected)
{
    m_clientConnected = connected;

    // Hide settings button when client is connected
    m_settingsButton->setVisible(!connected && m_serverRunning);

    if (connected) {
        m_loadingLabel->setText("Device connected - receiving stream...");

        showStreamingView();
    } else {
        m_loadingLabel->setText("Waiting for device connection...");
        m_videoLabel->clear();
        showTutorialView();
    }
}
#ifdef __linux__

void AirPlayWidget::onV4L2CheckboxToggled(bool enabled)
{
    if (enabled) {
        // Check if V4L2 loopback exists
        if (!checkV4L2LoopbackExists()) {
            // Show message and ask to create V4L2 loopback
            QMessageBox::StandardButton reply = QMessageBox::question(
                this, "V4L2 Loopback Required",
                "Virtual camera device is required for V4L2 output.\n\n"
                "This will create a virtual camera that other applications can "
                "use "
                "to receive the AirPlay stream. The operation requires "
                "administrator privileges.\n\n"
                "Do you want to create the virtual camera device?",
                QMessageBox::Yes | QMessageBox::No, QMessageBox::Yes);

            if (reply == QMessageBox::Yes) {
                if (createV4L2Loopback()) {
                    m_v4l2_enabled = true;

                } else {
                    m_v4l2Checkbox->setChecked(false);
                    m_v4l2_enabled = false;
                    QMessageBox::warning(
                        this, "Error",
                        "Failed to create virtual camera device. Please ensure "
                        "you have the necessary permissions.");
                }
            } else {
                m_v4l2Checkbox->setChecked(false);
                m_v4l2_enabled = false;
            }
        } else {
            m_v4l2_enabled = true;
        }
    } else {
        m_v4l2_enabled = false;
        closeV4L2();
    }
}
#endif

// AirPlayServerThread implementation
AirPlayServerThread::AirPlayServerThread(QObject *parent)
    : QThread(parent), m_shouldStop(false)
{
}

AirPlayServerThread::~AirPlayServerThread()
{
    uxplay_cleanup();
    wait();
}

void AirPlayServerThread::setArguments(const QStringList &args)
{
    QMutexLocker locker(&m_mutex);

    m_argData.clear();
    m_argv.clear();

    m_argData.append("uxplay");

    // Add all arguments
    for (const QString &arg : args) {
        m_argData.append(arg.toUtf8());
    }

    // Build argv array with persistent pointers
    for (QByteArray &data : m_argData) {
        m_argv.append(data.data());
    }
}

// Global pointer to current server thread for callbacks
static AirPlayServerThread *g_currentServerThread = nullptr;

void frame_callback(const unsigned char *data, int width, int height,
                    int stride, int format)
{
    if (!g_currentServerThread)
        return;
    QByteArray frameData((const char *)data, width * height * 3);
    emit g_currentServerThread->videoFrameReady(frameData, width, height);
}

void connection_callback(bool connected)
{
    qDebug() << "Connection callback: "
             << (connected ? "Connected" : "Disconnected");
    if (!g_currentServerThread)
        return;
    emit g_currentServerThread->clientConnectionChanged(connected);
}

void AirPlayServerThread::run()
{
    g_currentServerThread = this;
    emit statusChanged(true);
    callbacks_t callbacks;
    callbacks.frame_callback = frame_callback;
    callbacks.connection_callback = connection_callback;
    uxplay_callbacks = &callbacks;

    qDebug() << "Starting AirPlay server with arguments:" << m_argv.size();
    for (int i = 0; i < m_argv.size(); ++i) {
        qDebug() << "  argv[" << i << "] =" << m_argv[i];
    }

    try {
        int res = init_uxplay(m_argv.size(), m_argv.data());
        qDebug() << "AirPlay server exited with code: " << res;
        if (res != 0) {
            emit errorOccurred("AirPlay server exited unexpectedly.");
        }
    } catch (const std::exception &e) {
        qDebug() << "Exception in AirPlay server thread: " << e.what();
        emit errorOccurred(
            QString("AirPlay server encountered an error: %1").arg(e.what()));
    }

    uxplay_callbacks = nullptr;
    g_currentServerThread = nullptr;
}

#ifdef __linux__
// V4L2 Implementation
void AirPlayWidget::initV4L2(int width, int height, const char *device)
{
    closeV4L2(); // Close previous device if any

    m_v4l2_fd = open(device, O_WRONLY);
    if (m_v4l2_fd < 0) {
        qWarning("Failed to open V4L2 device %s: %s", device, strerror(errno));
        return;
    }

    struct v4l2_format fmt;
    memset(&fmt, 0, sizeof(fmt));
    fmt.type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
    fmt.fmt.pix.width = width;
    fmt.fmt.pix.height = height;
    fmt.fmt.pix.pixelformat = V4L2_PIX_FMT_RGB24;
    fmt.fmt.pix.field = V4L2_FIELD_NONE;
    fmt.fmt.pix.bytesperline = width * 3;
    fmt.fmt.pix.sizeimage = (unsigned int)width * height * 3;

    if (ioctl(m_v4l2_fd, VIDIOC_S_FMT, &fmt) < 0) {
        qWarning("Failed to set V4L2 format: %s", strerror(errno));
        ::close(m_v4l2_fd);
        m_v4l2_fd = -1;
        return;
    }

    m_v4l2_width = width;
    m_v4l2_height = height;
    qDebug("V4L2 device %s initialized to %dx%d", device, width, height);
}

void AirPlayWidget::closeV4L2()
{
    if (m_v4l2_fd >= 0) {
        ::close(m_v4l2_fd);
        m_v4l2_fd = -1;
    }
}

void AirPlayWidget::writeFrameToV4L2(uint8_t *data, int width, int height)
{
    // Check if V4L2 device needs to be initialized or re-initialized
    if (m_v4l2_fd < 0 || m_v4l2_width != width || m_v4l2_height != height) {
        initV4L2(width, height, "/dev/video0"); // Use your v4l2loopback device
    }

    // Write frame to V4L2 device if it's open
    if (m_v4l2_fd >= 0) {
        ssize_t bytes_written =
            write(m_v4l2_fd, data, (size_t)width * height * 3);
        if (bytes_written < 0) {
            qWarning("Failed to write frame to V4L2 device: %s",
                     strerror(errno));
            closeV4L2(); // Close on error to retry initialization
        }
    }
}

bool AirPlayWidget::checkV4L2LoopbackExists()
{
    try {
        QFileInfo videoDevice("/dev/video0");
        return videoDevice.exists();
    } catch (...) {
        qWarning("Exception occurred while checking for V4L2 loopback device");
        return false;
    }
}

bool AirPlayWidget::createV4L2Loopback()
{
    try {
        QProcess process;

        // Use pkexec to run modprobe with administrator privileges
        QStringList arguments;
        arguments << "modprobe" << "v4l2loopback" << "devices=1"
                  << "video_nr=0" << "card_label=\"iDescriptor Virtual Camera\""
                  << "exclusive_caps=1";

        process.start("pkexec", arguments);

        if (!process.waitForStarted(5000)) {
            qWarning("Failed to start pkexec process");
            return false;
        }

        if (!process.waitForFinished(10000)) {
            qWarning("Timeout waiting for modprobe to complete");
            process.kill();
            return false;
        }

        int exitCode = process.exitCode();
        if (exitCode != 0) {
            QString errorOutput = process.readAllStandardError();
            qWarning("modprobe failed with exit code %d: %s", exitCode,
                     errorOutput.toUtf8().constData());
            return false;
        }

        // Wait a bit for the device to be created
        QThread::msleep(500);

        // Verify the device was created
        return checkV4L2LoopbackExists();

    } catch (...) {
        qWarning("Exception occurred while creating V4L2 loopback device");
        return false;
    }
}

void AirPlayWidget::setupV4L2Checkbox()
{
    if (!SettingsManager::sharedInstance()->showV4L2())
        return;

    try {
        m_v4l2Checkbox = new QCheckBox("Enable V4L2 Virtual Camera Output");
        m_v4l2Checkbox->setToolTip("Enable output to virtual camera device "
                                   "that other applications can use");
        m_v4l2Checkbox->setChecked(false);

        connect(m_v4l2Checkbox, &QCheckBox::toggled, this,
                &AirPlayWidget::onV4L2CheckboxToggled);

    } catch (...) {
        qWarning("Exception occurred while setting up V4L2 checkbox");
    }
}
#endif
