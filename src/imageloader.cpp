#include "imageloader.h"
#include "iDescriptor.h"
#include "imagetask.h"
#include <QBuffer>
#include <QImageReader>
#include <QPixmap>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/display.h>
#include <libavutil/imgutils.h>
#include <libswscale/swscale.h>
}

ImageLoader::ImageLoader(QObject *parent) : QObject(parent)
{
    // TODO: maybe finetune to hardware ?
    m_pool.setMaxThreadCount(15);

    if (qApp) {
        connect(qApp, &QCoreApplication::aboutToQuit, this,
                [this]() { clear(); });
    }
}

bool ImageLoader::isLoading(const QString &path)
{
    QMutexLocker locker(&m_mutex);
    return m_pendingTasks.contains(path);
}

void ImageLoader::requestThumbnail(
    const std::shared_ptr<iDescriptorDevice> device, const QString &path,
    unsigned int row)
{
    {
        QMutexLocker locker(&m_mutex);
        if (m_pendingTasks.contains(path))
            return;
    }

    auto *task = new ImageTask(device, path, row);

    {
        QMutexLocker locker(&m_mutex);
        m_pendingTasks[path] = task;
    }

    connect(task, &ImageTask::finished, this, &ImageLoader::onTaskFinished,
            Qt::QueuedConnection);

    // Use row as priority
    m_pool.start(task, row);
}

/*
    this method should not load from cache
    because cached images are already scaled down
    we need the original image
*/
void ImageLoader::requestImageWithCallback(
    const std::shared_ptr<iDescriptorDevice> device, const QString &path,
    int priority, std::function<void(const QPixmap &)> callback,
    std::optional<std::shared_ptr<CXX::HauseArrest>> hause_arrest, bool useAfc2)
{
    auto *task =
        new ImageTask(device, path, priority, false, hause_arrest, useAfc2);

    connect(
        task, &ImageTask::finished, this,
        [callback](const QString &, const QImage &image, unsigned int) {
            if (QCoreApplication::closingDown() ||
                !QGuiApplication::instance()) {
                callback(QPixmap());
                return;
            }
            callback(image.isNull() ? QPixmap() : QPixmap::fromImage(image));
        },
        Qt::QueuedConnection);

    m_pool.start(task, priority);
}

void ImageLoader::cancelThumbnail(const QString &path)
{
    qDebug() << "Attempting to cancel thumbnail loading for" << path;

    ImageTask *task = nullptr;
    {
        QMutexLocker locker(&m_mutex);
        task = m_pendingTasks.take(path);
    }

    if (!task) {
        return;
    }

    if (m_pool.tryTake(task)) {
        qDebug() << "Cancelled thumbnail loading for" << path;
        // should be safe to delete
        delete task;
    }
}

void ImageLoader::clear()
{
    qDebug() << "Clearing ImageLoader cache and pending tasks";
    m_pool.clear();
    m_pool.waitForDone();

    QMutexLocker locker(&m_mutex);
    m_pendingTasks.clear();
}

void ImageLoader::onTaskFinished(const QString &path, const QImage &image,
                                 unsigned int row)
{
    {
        QMutexLocker locker(&m_mutex);
        if (!m_pendingTasks.contains(path)) {
            return;
        }
        m_pendingTasks.remove(path);
    }

    if (QCoreApplication::closingDown() || !QGuiApplication::instance()) {
        return;
    }

    const QPixmap pixmap =
        image.isNull() ? QPixmap() : QPixmap::fromImage(image);
    emit thumbnailReady(path, pixmap, row);
}

// almost a copy of loadThumbnailFromDevice but without any scaling logic
QImage ImageLoader::loadImage(
    const std::shared_ptr<iDescriptorDevice> device, const QString &filePath,
    std::optional<std::shared_ptr<CXX::HauseArrest>> hause_arrest, bool useAfc2)
{
    if (QCoreApplication::closingDown() || !QGuiApplication::instance()) {
        return {};
    }
    QByteArray imageData;

    if (useAfc2) {
        imageData = device->afc2_backend->file_to_buffer(filePath);
    } else if (hause_arrest.has_value() && hause_arrest.value()) {
        qDebug() << "Loading image using HauseArrest for:" << filePath;
        imageData = hause_arrest.value()->file_to_buffer(filePath);
    } else {
        imageData = device->afc_backend->file_to_buffer(filePath);
    }

    if (filePath.endsWith(".HEIC", Qt::CaseInsensitive)) {
        QImage img = load_heic(imageData);
        return img.isNull() ? QImage() : img;
    }

    QBuffer buffer(&imageData);
    buffer.open(QIODevice::ReadOnly);

    QImageReader reader(&buffer);
    if (reader.canRead()) {
        QImage image = reader.read();
        if (!image.isNull()) {
            return image;
        }
    }

    QImage fallback;
    if (fallback.loadFromData(imageData)) {
        return fallback;
    }

    return {};
}

QImage ImageLoader::loadThumbnailFromDevice(
    const std::shared_ptr<iDescriptorDevice> device, const QString &filePath,
    const QSize &size,
    std::optional<std::shared_ptr<CXX::HauseArrest>> hause_arrest, bool useAfc2)
{
    if (QCoreApplication::closingDown() || !QGuiApplication::instance()) {
        return {};
    }

    QByteArray imageData;

    if (useAfc2) {
        imageData = device->afc2_backend->file_to_buffer(filePath);
    } else if (hause_arrest.has_value() && hause_arrest.value()) {
        qDebug() << "Loading thumbnail using HauseArrest for:" << filePath;
        imageData = hause_arrest.value()->file_to_buffer(filePath);
    } else {
        imageData = device->afc_backend->file_to_buffer(filePath);
    }

    if (filePath.endsWith(".HEIC", Qt::CaseInsensitive)) {
        QImage img = load_heic(imageData);
        return img.isNull() ? QImage()
                            : img.scaled(size, Qt::KeepAspectRatio,
                                         Qt::SmoothTransformation);
    }

    QBuffer buffer(&imageData);
    buffer.open(QIODevice::ReadOnly);

    QImageReader reader(&buffer);
    if (reader.canRead()) {
        QImage image = reader.read();
        if (!image.isNull()) {
            return image.scaled(size, Qt::KeepAspectRatio,
                                Qt::SmoothTransformation);
        }
    }

    QImage fallback;
    if (fallback.loadFromData(imageData)) {
        return fallback.scaled(size, Qt::KeepAspectRatio,
                               Qt::SmoothTransformation);
    }

    return {};
}

QImage ImageLoader::generateVideoThumbnailFFmpeg(
    const std::shared_ptr<iDescriptorDevice> device, const QString &filePath,
    const QSize &requestedSize,
    std::optional<std::shared_ptr<CXX::HauseArrest>> hause_arrest, bool useAfc2)
{
    QImage thumbnail;
    if (QCoreApplication::closingDown()) {
        qDebug() << "Application is closing, aborting "
                    "generateVideoThumbnailFFmpeg for"
                 << filePath;
        return thumbnail;
    }

    /*
        FIXME: other afc clients are not respected here, we need to handle this
        better, currently only the normal afc client is used for video thumbnail
        generation
    */
    CXX::AfcBackend *afc = device->afc_backend;

    const qint64 fileSize = afc->get_file_size(filePath);
    if (fileSize <= 0) {
        qWarning() << "Invalid video file size for thumbnail:" << filePath;
        return {};
    }

    AVFormatContext *formatCtx = avformat_alloc_context();
    if (!formatCtx) {
        qWarning() << "Failed to allocate format context";
        return {};
    }

    struct StreamContext {
        CXX::AfcBackend *backend;
        QString path;
        qint64 fileSize;
        qint64 currentPos;
    };

    auto *streamCtx = new StreamContext{afc, filePath, fileSize, 0};

    auto readPacket = [](void *opaque, uint8_t *buf, int bufSize) -> int {
        auto *ctx = static_cast<StreamContext *>(opaque);

        if (ctx->currentPos >= ctx->fileSize) {
            return AVERROR_EOF;
        }

        qint64 toRead =
            std::min<qint64>(bufSize, ctx->fileSize - ctx->currentPos);
        QByteArray chunk =
            ctx->backend->read_file_range(ctx->path, ctx->currentPos, toRead);

        if (chunk.isEmpty()) {
            // IO error
            return AVERROR(EIO);
        }

        const int n = std::min<int>(chunk.size(), bufSize);
        memcpy(buf, chunk.constData(), n);
        ctx->currentPos += n;
        return n;
    };

    auto seekPacket = [](void *opaque, int64_t offset, int whence) -> int64_t {
        auto *ctx = static_cast<StreamContext *>(opaque);

        if (whence == AVSEEK_SIZE) {
            return ctx->fileSize;
        }

        qint64 newPos = 0;
        switch (whence) {
        case SEEK_SET:
            newPos = offset;
            break;
        case SEEK_CUR:
            newPos = ctx->currentPos + offset;
            break;
        case SEEK_END:
            newPos = ctx->fileSize + offset;
            break;
        default:
            return -1;
        }

        if (newPos < 0 || newPos > ctx->fileSize) {
            return -1;
        }

        ctx->currentPos = newPos;
        return newPos;
    };

    const int avioBufferSize = 32768;
    unsigned char *avioBuffer =
        static_cast<unsigned char *>(av_malloc(avioBufferSize));
    if (!avioBuffer) {
        delete streamCtx;
        avformat_free_context(formatCtx);
        return {};
    }

    AVIOContext *avioCtx =
        avio_alloc_context(avioBuffer, avioBufferSize, 0, streamCtx, readPacket,
                           nullptr, seekPacket);
    if (!avioCtx) {
        av_free(avioBuffer);
        delete streamCtx;
        avformat_free_context(formatCtx);
        return {};
    }

    formatCtx->pb = avioCtx;
    formatCtx->flags |= AVFMT_FLAG_CUSTOM_IO;

    // Open input
    if (avformat_open_input(&formatCtx, nullptr, nullptr, nullptr) < 0) {
        qWarning() << "Failed to open video format";
        av_free(avioCtx->buffer);
        avio_context_free(&avioCtx);
        avformat_free_context(formatCtx);
        return {};
    }

    // Find stream info
    if (avformat_find_stream_info(formatCtx, nullptr) < 0) {
        qWarning() << "Failed to find stream info";
        avformat_close_input(&formatCtx);
        av_free(avioCtx->buffer);
        avio_context_free(&avioCtx);
        return {};
    }

    // Find video stream
    int videoStreamIndex = -1;
    const AVCodec *codec = nullptr;
    AVCodecParameters *codecParams = nullptr;

    for (unsigned int i = 0; i < formatCtx->nb_streams; i++) {
        if (formatCtx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
            videoStreamIndex = i;
            codecParams = formatCtx->streams[i]->codecpar;
            codec = avcodec_find_decoder(codecParams->codec_id);
            break;
        }
    }

    if (videoStreamIndex == -1 || !codec) {
        qWarning() << "No video stream found";
        avformat_close_input(&formatCtx);
        av_free(avioCtx->buffer);
        avio_context_free(&avioCtx);
        return {};
    }

    // Allocate codec context
    AVCodecContext *codecCtx = avcodec_alloc_context3(codec);
    if (!codecCtx) {
        avformat_close_input(&formatCtx);
        av_free(avioCtx->buffer);
        avio_context_free(&avioCtx);
        return {};
    }

    if (avcodec_parameters_to_context(codecCtx, codecParams) < 0) {
        avcodec_free_context(&codecCtx);
        avformat_close_input(&formatCtx);
        av_free(avioCtx->buffer);
        avio_context_free(&avioCtx);
        return {};
    }

    // Open codec
    if (avcodec_open2(codecCtx, codec, nullptr) < 0) {
        avcodec_free_context(&codecCtx);
        avformat_close_input(&formatCtx);
        av_free(avioCtx->buffer);
        avio_context_free(&avioCtx);
        return {};
    }

    // Allocate frame
    AVFrame *frame = av_frame_alloc();
    AVPacket *packet = av_packet_alloc();

    if (!frame || !packet) {
        if (frame)
            av_frame_free(&frame);
        if (packet)
            av_packet_free(&packet);
        avcodec_free_context(&codecCtx);
        avformat_close_input(&formatCtx);
        av_free(avioCtx->buffer);
        avio_context_free(&avioCtx);
        return {};
    }

    // Read frames until we get a valid one
    bool frameDecoded = false;
    while (av_read_frame(formatCtx, packet) >= 0) {
        if (packet->stream_index == videoStreamIndex) {
            if (avcodec_send_packet(codecCtx, packet) >= 0) {
                if (avcodec_receive_frame(codecCtx, frame) >= 0) {
                    frameDecoded = true;
                    av_packet_unref(packet);
                    break;
                }
            }
        }
        av_packet_unref(packet);
    }

    if (frameDecoded) {
        // Get rotation from display matrix
        double rotation = 0.0;
        if (AVFrameSideData *sd =
                av_frame_get_side_data(frame, AV_FRAME_DATA_DISPLAYMATRIX)) {
            rotation =
                -av_display_rotation_get(reinterpret_cast<int32_t *>(sd->data));
        }

        // Convert frame to RGB24
        SwsContext *swsCtx =
            sws_getContext(frame->width, frame->height,
                           static_cast<AVPixelFormat>(frame->format),
                           frame->width, frame->height, AV_PIX_FMT_RGB24,
                           SWS_BILINEAR, nullptr, nullptr, nullptr);

        if (swsCtx) {
            AVFrame *rgbFrame = av_frame_alloc();
            if (rgbFrame) {
                rgbFrame->format = AV_PIX_FMT_RGB24;
                rgbFrame->width = frame->width;
                rgbFrame->height = frame->height;

                if (av_frame_get_buffer(rgbFrame, 0) >= 0) {
                    sws_scale(swsCtx, frame->data, frame->linesize, 0,
                              frame->height, rgbFrame->data,
                              rgbFrame->linesize);

                    // Convert to QImage
                    QImage img(rgbFrame->data[0], rgbFrame->width,
                               rgbFrame->height, rgbFrame->linesize[0],
                               QImage::Format_RGB888);

                    // Create a deep copy since AVFrame will be freed
                    QImage imgCopy = img.copy();

                    // Apply rotation
                    if (rotation != 0.0) {
                        QTransform transform;
                        transform.rotate(rotation);
                        imgCopy = imgCopy.transformed(transform);
                    }

                    // Scale to requested size
                    /*
                        TODO: scaling might become optional
                        if we ever needed the raw frame,
                        might need to abstract the main logic to get the frame
                        and handle scaling separately
                    */
                    thumbnail =
                        imgCopy.scaled(requestedSize, Qt::KeepAspectRatio,
                                       Qt::SmoothTransformation);
                }

                av_frame_free(&rgbFrame);
            }

            sws_freeContext(swsCtx);
        }
    }

    // Cleanup
    av_frame_free(&frame);
    av_packet_free(&packet);
    avcodec_free_context(&codecCtx);
    avformat_close_input(&formatCtx);

    return thumbnail;
}