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

#ifndef AIRPLAYWIDGET_H
#define AIRPLAYWIDGET_H

#include "iDescriptor-ui.h"
#include "qprocessindicator.h"
#include <QCheckBox>
#include <QCloseEvent>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QLabel>
#include <QMainWindow>
#include <QMediaPlayer>
#include <QMutex>
#include <QPushButton>
#include <QStackedWidget>
#include <QThread>
#include <QTimer>
#include <QVBoxLayout>
#include <QVideoWidget>
#include <QWaitCondition>

class AirPlayServerThread : public QThread
{
    Q_OBJECT

public:
    explicit AirPlayServerThread(QObject *parent = nullptr);
    ~AirPlayServerThread();

    // void stopServer();
    void setArguments(const QStringList &args);

signals:
    void statusChanged(bool running);
    void videoFrameReady(QByteArray frameData, int width, int height);
    void clientConnectionChanged(bool connected);
    void errorOccurred(const QString &message);

protected:
    void run() override;

private:
    QMutex m_mutex;
    QWaitCondition m_waitCondition;
    bool m_shouldStop;
    QVector<QByteArray> m_argData;
    QVector<char *> m_argv;
};

class AirPlaySettings
{
public:
    explicit AirPlaySettings();
    int fps;
    bool noHold;

    QStringList toArgs() const;
};

class AirPlaySettingsDialog : public QDialog
{
    Q_OBJECT
public:
    explicit AirPlaySettingsDialog(QWidget *parent = nullptr);
    AirPlaySettings getSettings() const;

private:
    void setupUI();

    QComboBox *m_fpsComboBox;
    QCheckBox *m_noHoldCheckbox;
    AirPlaySettings m_settings;
};

class AirPlayWidget : public Tool
{
    Q_OBJECT

public:
    explicit AirPlayWidget(QWidget *parent = nullptr);
    ~AirPlayWidget();

private slots:
    void updateVideoFrame(QByteArray frameData, int width, int height);
    void onServerStatusChanged(bool running);
    void onClientConnectionChanged(bool connected);
    void showSettingsDialog();
#ifdef __linux__
    void onV4L2CheckboxToggled(bool enabled);
#endif

private:
    void setupUI();
    void setupTutorialVideo();
    void showTutorialView();
    void showStreamingView();
    void startAirPlayServer();
    void stopAirPlayServer();

#ifdef __linux__
    void initV4L2(int width, int height, const char *device = "/dev/video0");
    void closeV4L2();
    void writeFrameToV4L2(uint8_t *data, int width, int height);
    bool checkV4L2LoopbackExists();
    bool createV4L2Loopback();
    void setupV4L2Checkbox();
#endif

    // UI Components
    QStackedWidget *m_stackedWidget;
    QWidget *m_tutorialWidget;
    QWidget *m_streamingWidget;

    QProcessIndicator *m_loadingIndicator;
    QLabel *m_loadingLabel;
    QMediaPlayer *m_tutorialPlayer;
    QVideoWidget *m_tutorialVideoWidget;
    QLabel *m_videoLabel;
    QVBoxLayout *m_tutorialLayout;
    QPushButton *m_settingsButton;

#ifdef __linux__
    QCheckBox *m_v4l2Checkbox;
    int m_v4l2_fd;
    int m_v4l2_width;
    int m_v4l2_height;
    bool m_v4l2_enabled = false;
#endif

    AirPlayServerThread *m_serverThread;
    bool m_serverRunning;
    bool m_clientConnected;
    AirPlaySettings m_settings;
};

#endif // AIRPLAYWIDGET_H
