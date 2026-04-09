#ifndef IMAGETASK_H
#define IMAGETASK_H

#include "iDescriptor-ui.h"
#include "iDescriptor.h"
#include "imageloader.h"
#include <QGuiApplication>
#include <QImage>
#include <QObject>
#include <QPixmap>
#include <QRunnable>
#include <QString>

class ImageTask : public QObject, public QRunnable
{
    Q_OBJECT
public:
    ImageTask(const std::shared_ptr<iDescriptorDevice> device,
              const QString &path, unsigned int row, bool scale = true,
              std::optional<std::shared_ptr<CXX::HauseArrest>> hause_arrest =
                  std::nullopt,
              bool useAfc2 = false)
        : m_device(device), m_path(path), m_isThumbnail(scale), m_row(row),
          m_hause_arrest(hause_arrest), m_useAfc2(useAfc2)
    {
        setAutoDelete(true);
    }

signals:
    void finished(const QString &path, const QImage &image, unsigned int row);

protected:
    void run() override
    {
        if (QCoreApplication::closingDown()) {
            return;
        }

        const bool isVideo = iDescriptor::Utils::isVideoFile(m_path);

        if (isVideo) {
            QImage image = ImageLoader::generateVideoThumbnailFFmpeg(
                m_device, m_path, THUMBNAIL_SIZE, m_hause_arrest, m_useAfc2);
            emit finished(m_path, image, m_row);
            return;
        }

        if (m_isThumbnail) {
            QImage image = ImageLoader::loadThumbnailFromDevice(
                m_device, m_path, THUMBNAIL_SIZE, m_hause_arrest, m_useAfc2);
            emit finished(m_path, image, m_row);
        } else {
            QImage image = ImageLoader::loadImage(m_device, m_path,
                                                  m_hause_arrest, m_useAfc2);
            emit finished(m_path, image, m_row);
        }
    }

private:
    const std::shared_ptr<iDescriptorDevice> m_device;
    QString m_path;
    bool m_isThumbnail;
    unsigned int m_row;
    std::optional<std::shared_ptr<CXX::HauseArrest>> m_hause_arrest;
    bool m_useAfc2;
};

#endif // IMAGETASK_H
