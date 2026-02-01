#ifndef IMAGELOADER_H
#define IMAGELOADER_H

#include "iDescriptor.h"
#include <QCache>
#include <QImage>
#include <QObject>
#include <QSet>
#include <QString>
#include <QThreadPool>

class ImageLoader : public QObject
{
    Q_OBJECT
public:
    enum LoadPriority { Low = 0, Medium = 50, High = 100 };
    Q_ENUM(LoadPriority)

    explicit ImageLoader(QObject *parent = nullptr);
    static ImageLoader &sharedInstance()
    {
        static ImageLoader instance;
        return instance;
    }
    void requestThumbnail(const iDescriptorDevice *device, const QString &path,
                          int priority, unsigned int row = 0);
    void cancelThumbnail(const QString &path);
    bool isLoading(const QString &path);
    void clear();
    QCache<QString, QPixmap> m_cache;
    static QPixmap loadThumbnailFromDevice(const iDescriptorDevice *device,
                                           const QString &filePath,
                                           const QSize &size);
    static QPixmap generateVideoThumbnailFFmpeg(const iDescriptorDevice *device,
                                                const QString &filePath,
                                                const QSize &size);
    static QPixmap loadImage(const iDescriptorDevice *device,
                             const QString &filePath);
signals:
    void thumbnailReady(const QString &path, const QPixmap &image,
                        unsigned int row);

private slots:
    void onTaskFinished(const QString &path, const QPixmap &image,
                        unsigned int row);

private:
    QThreadPool m_pool;
    QSet<QString> m_pending;
};

#endif // IMAGELOADER_H