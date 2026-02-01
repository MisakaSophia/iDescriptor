#include "imageloader.h"
#include "iDescriptor.h"
#include "imagetask.h"
#include "servicemanager.h"
#include <QBuffer>
#include <QImageReader>
#include <QPixmap>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/imgutils.h>
#include <libswscale/swscale.h>
}

ImageLoader::ImageLoader(QObject *parent) : QObject(parent)
{
    m_pool.setMaxThreadCount(10);
    // 350 MB cache for thumbnails
    m_cache.setMaxCost(350 * 1024 * 1024);
}

bool ImageLoader::isLoading(const QString &path)
{
    return m_pending.contains(path);
}

void ImageLoader::requestThumbnail(const iDescriptorDevice *device,
                                   const QString &path, int priority,
                                   unsigned int row)
{
    if (auto *cached = m_cache.object(path)) {
        emit thumbnailReady(path, *cached, row);
        return;
    }

    if (m_pending.contains(path))
        return;

    m_pending.insert(path);

    // FIXME: qsize
    auto *task = new ImageTask(device, path, QSize(128, 128), row);

    connect(task, &ImageTask::finished, this, &ImageLoader::onTaskFinished,
            Qt::QueuedConnection);

    m_pool.start(task, priority);
}

void ImageLoader::cancelThumbnail(const QString &path)
{
    m_pending.remove(path);
}

void ImageLoader::clear()
{
    m_pending.clear();
    m_cache.clear();
}

void ImageLoader::onTaskFinished(const QString &path, const QPixmap &pixmap,
                                 unsigned int row)
{
    // Stale?
    if (!m_pending.contains(path))
        return;

    m_pending.remove(path);

    // Cache
    m_cache.insert(path, new QPixmap(pixmap));

    emit thumbnailReady(path, pixmap, row);
}

// Static function that runs in worker thread
QPixmap ImageLoader::loadThumbnailFromDevice(const iDescriptorDevice *device,
                                             const QString &filePath,
                                             const QSize &size)
{
    // Load from device using ServiceManager
    QByteArray imageData = ServiceManager::safeReadAfcFileToByteArray(
        device, filePath.toUtf8().constData());

    if (imageData.isEmpty()) {
        qDebug() << "Could not read from device:" << filePath;
        return {}; // Return empty pixmap on error
    }

    if (filePath.endsWith(".HEIC", Qt::CaseInsensitive)) {
        QPixmap img = load_heic(imageData);
        return img.isNull() ? QPixmap()
                            : img.scaled(size, Qt::KeepAspectRatio,
                                         Qt::SmoothTransformation);
    }

    // Use QImageReader for efficient, low-memory scaled loading
    QBuffer buffer(&imageData);
    buffer.open(QIODevice::ReadOnly);

    QImageReader reader(&buffer);
    if (reader.canRead()) {
        // This is the key optimization: it decodes a smaller image directly,
        // saving a massive amount of memory.
        reader.setScaledSize(size);
        QImage image = reader.read();
        if (!image.isNull()) {
            return QPixmap::fromImage(image);
        }
        qDebug() << "QImageReader failed to decode" << filePath
                 << "Error:" << reader.errorString();
    }

    // Fallback for formats QImageReader might struggle with
    QPixmap original;
    if (original.loadFromData(imageData)) {
        return original.scaled(size, Qt::KeepAspectRatio,
                               Qt::SmoothTransformation);
    }

    qDebug() << "Could not decode image data for:" << filePath;
    return {};
}

QPixmap
ImageLoader::generateVideoThumbnailFFmpeg(const iDescriptorDevice *device,
                                          const QString &filePath,
                                          const QSize &requestedSize)
{
    QPixmap thumbnail;

    AfcFileHandle *fileHandle = nullptr;

    IdeviceFfiError *err_open = // Use distinct variable name for clarity
        ServiceManager::safeAfcFileOpen(device, filePath.toUtf8().constData(),
                                        AfcFopenMode::AfcRdOnly, &fileHandle);

    if (err_open || fileHandle == nullptr) {
        qWarning() << "Failed to open video file for thumbnail:" << filePath;
        if (err_open) {
            idevice_error_free(err_open);
        }
        return {};
    }

    // Get file size
    AfcFileInfo info = {};
    IdeviceFfiError
        *err_info = // Use distinct variable name for the error from GetFileInfo
        ServiceManager::safeAfcGetFileInfo(
            device, filePath.toUtf8().constData(), &info);

    uint64_t fileSize = 0;
    if (err_info) {
        qWarning() << "Failed to get file info for thumbnail:" << filePath
                   << "Error:" << err_info->message;
        idevice_error_free(err_info);
        ServiceManager::safeAfcFileClose(device, fileHandle);
        afc_file_info_free(&info); // Free internal strings of info
        return {};
    }

    fileSize = info.size;
    afc_file_info_free(&info); // Free internal strings of info after use

    if (fileSize == 0) {
        ServiceManager::safeAfcFileClose(device, fileHandle);
        qWarning() << "Invalid video file size for thumbnail:" << filePath;
        return {};
    }

    // Create custom AVIOContext for reading from device on-demand
    AVFormatContext *formatCtx = avformat_alloc_context();
    if (!formatCtx) {
        ServiceManager::safeAfcFileClose(device, fileHandle);
        qWarning() << "Failed to allocate format context";
        return {};
    }

    // Context for streaming read from device
    struct StreamContext {
        const iDescriptorDevice *device;
        AfcFileHandle *fileHandle;
        uint64_t fileSize;
        uint64_t currentPos;
    };

    StreamContext *streamCtx =
        new StreamContext{device, fileHandle, fileSize, 0};

    // Custom read function that reads from device on-demand
    auto readPacket = [](void *opaque, uint8_t *buf, int bufSize) -> int {
        StreamContext *ctx = static_cast<StreamContext *>(opaque);

        if (ctx->currentPos >= ctx->fileSize) {
            return AVERROR_EOF;
        }

        uint32_t toRead =
            std::min(static_cast<uint32_t>(bufSize),
                     static_cast<uint32_t>(ctx->fileSize - ctx->currentPos));
        size_t bytesRead = 0;
        uint8_t *read_data_ptr =
            nullptr; // Pointer to store the data allocated by safeAfcFileRead

        // Call safeAfcFileRead to get the data into a newly allocated buffer
        IdeviceFfiError *err = ServiceManager::safeAfcFileRead(
            ctx->device, ctx->fileHandle, &read_data_ptr, toRead, &bytesRead);

        if (err) {
            qWarning() << "AFC read error in readPacket for file handle"
                       << ctx->fileHandle << ":" << err->message;
            idevice_error_free(err);
            return AVERROR(EIO);
        }

        if (bytesRead == 0) {
            // If bytesRead is 0 but we expected to read more, it's an error.
            // If currentPos is already at fileSize, it's EOF.
            if (ctx->currentPos < ctx->fileSize) {
                qWarning() << "AFC readPacket returned 0 bytes but not EOF, "
                              "expected toRead:"
                           << toRead << "currentPos:" << ctx->currentPos
                           << "fileSize:" << ctx->fileSize;
                // Free the allocated pointer if safeAfcFileRead still allocates
                // even for 0 bytes.
                if (read_data_ptr) {
                    afc_file_read_data_free(read_data_ptr, 0);
                }
                return AVERROR(EIO);
            }
            // It's EOF
            return AVERROR_EOF;
        }

        // Copy the data from the newly allocated `read_data_ptr` into FFmpeg's
        // `buf`
        if (read_data_ptr) {
            memcpy(buf, read_data_ptr, bytesRead);
            afc_file_read_data_free(
                read_data_ptr,
                bytesRead); // Free the memory allocated by safeAfcFileRead
        } else {
            qWarning() << "AFC readPacket: read_data_ptr was null but "
                          "bytesRead > 0. This is unexpected.";
            return AVERROR(EIO);
        }

        ctx->currentPos += bytesRead;
        return static_cast<int>(bytesRead);
    };

    // Custom seek function using AFC seek
    auto seekPacket = [](void *opaque, int64_t offset, int whence) -> int64_t {
        StreamContext *ctx = static_cast<StreamContext *>(opaque);

        if (whence == AVSEEK_SIZE) {
            return static_cast<int64_t>(ctx->fileSize);
        }

        int64_t newPos = 0;
        int seekWhence = SEEK_SET;

        if (whence == SEEK_SET) {
            newPos = offset;
            seekWhence = SEEK_SET;
        } else if (whence == SEEK_CUR) {
            newPos = static_cast<int64_t>(ctx->currentPos) + offset;
            seekWhence = SEEK_SET;
        } else if (whence == SEEK_END) {
            newPos = static_cast<int64_t>(ctx->fileSize) + offset;
            seekWhence = SEEK_SET;
        } else {
            return -1;
        }

        if (newPos < 0 || newPos > static_cast<int64_t>(ctx->fileSize)) {
            return -1;
        }

        IdeviceFfiError *err = ServiceManager::safeAfcFileSeek(
            ctx->device, ctx->fileHandle, newPos, seekWhence);

        if (err) {
            qDebug() << "AFC seek error:" << err->message
                     << "code:" << err->code;
            // idevice_error_free(err);
            return -1;
        }

        ctx->currentPos = static_cast<uint64_t>(newPos);
        return newPos;
    };

    const int avioBufferSize = 32768; // 32KB buffer for streaming
    unsigned char *avioBuffer =
        static_cast<unsigned char *>(av_malloc(avioBufferSize));
    if (!avioBuffer) {
        delete streamCtx;
        ServiceManager::safeAfcFileClose(device, fileHandle);
        avformat_free_context(formatCtx);
        return {};
    }

    AVIOContext *avioCtx =
        avio_alloc_context(avioBuffer, avioBufferSize, 0, streamCtx, readPacket,
                           nullptr, seekPacket);

    if (!avioCtx) {
        av_free(avioBuffer);
        delete streamCtx;
        ServiceManager::safeAfcFileClose(device, fileHandle);
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

                    // Scale to requested size
                    thumbnail = QPixmap::fromImage(
                        imgCopy.scaled(requestedSize, Qt::KeepAspectRatio,
                                       Qt::SmoothTransformation));
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

    // Close the AFC file handle
    ServiceManager::safeAfcFileClose(device, fileHandle);

    // Free AVIO context and stream context
    av_free(avioCtx->buffer);
    avio_context_free(&avioCtx);
    delete streamCtx;

    return thumbnail;
}

// todo make sure this is also a task used in mediapreviewdialog.cpp
QPixmap ImageLoader::loadImage(const iDescriptorDevice *device,
                               const QString &filePath)
{
    QByteArray imageData = ServiceManager::safeReadAfcFileToByteArray(
        device, filePath.toUtf8().constData());

    if (imageData.isEmpty()) {
        qDebug() << "Could not read from device:" << filePath;
        return QPixmap(); // Return empty pixmap on error
    }

    if (filePath.endsWith(".HEIC")) {
        qDebug() << "Loading HEIC image from data for:" << filePath;
        QPixmap img = load_heic(imageData);
        return img.isNull() ? QPixmap() : img;
    }

    QPixmap original;
    if (!original.loadFromData(imageData)) {
        qDebug() << "Could not decode image data for:" << filePath;
        return QPixmap();
    }

    return original;
}