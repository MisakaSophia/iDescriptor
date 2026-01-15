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

#ifndef LIVESCREEN_H
#define LIVESCREEN_H

#include "iDescriptor.h"
#include "servicemanager.h"
#include <QLabel>
#include <QThread>
#include <QTimer>
#include <QWidget>

class ScreenshotrThread : public QThread
{
    Q_OBJECT
public:
    explicit ScreenshotrThread(ScreenshotrClientHandle *client,
                               iDescriptorDevice *device,
                               QObject *parent = nullptr)
        : QThread(parent), m_device(device), m_client(client), m_fps(15)
    {
    }

protected:
    void run() override
    {
        qDebug() << "Started capturing";

        // Thread loop to continuously fetch screenshots
        while (!isInterruptionRequested()) {
            ScreenshotData screenshotData;
            IdeviceFfiError *err = ServiceManager::takeScreenshot(
                m_device, m_client, &screenshotData);
            if (!err && screenshotData.data && screenshotData.length > 0) {
                QByteArray byteArray(
                    reinterpret_cast<const char *>(screenshotData.data),
                    static_cast<int>(screenshotData.length));
                QImage image;
                image.loadFromData(byteArray);
                QPixmap pixmap = QPixmap::fromImage(image);
                emit screenshotCaptured(pixmap);
                screenshotr_screenshot_free(screenshotData);
            } else {
                qDebug() << "Failed to capture screenshot";
            }
            msleep(1000 / m_fps); // Capture at ~m_fps FPS
        }
    }
signals:
    void screenshotCaptured(const QPixmap &pixmap);

private:
    ScreenshotrClientHandle *m_client;
    int m_fps;
    iDescriptorDevice *m_device;
};

class LiveScreenWidget : public QWidget
{
    Q_OBJECT
public:
    explicit LiveScreenWidget(iDescriptorDevice *device,
                              QWidget *parent = nullptr);
    ~LiveScreenWidget();

private:
    bool initializeScreenshotService(bool notify);
    void updateScreenshot();
    void startCapturing();

    iDescriptorDevice *m_device;
    QLabel *m_imageLabel;
    QLabel *m_statusLabel;
    ScreenshotrClientHandle *m_screenshotrClient = nullptr;
    ScreenshotrThread *m_thread = nullptr;

private:
    void startInitialization();
};

#endif // LIVESCREEN_H
