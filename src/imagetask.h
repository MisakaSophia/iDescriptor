#ifndef IMAGETASK_H
#define IMAGETASK_H

#include "iDescriptor.h"
#include <QImage>
#include <QObject>
#include <QPixmap>
#include <QRunnable>
#include <QString>

#include "imageloader.h"

class ImageTask : public QObject, public QRunnable
{
    Q_OBJECT
public:
    ImageTask(const iDescriptorDevice *device, const QString &path,
              const QSize &thumbnailSize, unsigned int row)
        : m_device(device), m_path(path), m_thumbnailSize(thumbnailSize),
          m_row(row)
    {
        setAutoDelete(true);
    }

signals:
    void finished(const QString &path, const QPixmap &image, unsigned int row);

protected:
    void run() override
    {
        bool isVideo = iDescriptor::Utils::isVideoFile(m_path);

        if (isVideo) {
            QPixmap thumbnail = ImageLoader::generateVideoThumbnailFFmpeg(
                m_device, m_path, m_thumbnailSize);

            emit finished(m_path, thumbnail, m_row);
        } else {
            QPixmap image = ImageLoader::loadThumbnailFromDevice(
                m_device, m_path, m_thumbnailSize);
            emit finished(m_path, image, m_row);
        }
    }

private:
    const iDescriptorDevice *m_device;
    QString m_path;
    QSize m_thumbnailSize;
    unsigned int m_row;
};

#endif // IMAGETASK_H
