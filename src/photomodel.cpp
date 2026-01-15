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

#include "photomodel.h"
#include "iDescriptor.h"
// #include "mediastreamermanager.h"
#include "servicemanager.h"
#include <QDebug>
#include <QEventLoop>
#include <QIcon>
#include <QImage>
#include <QImageReader>
#include <QMediaPlayer>
#include <QPixmap>
#include <QRegularExpression>
#include <QSemaphore>
#include <QTimer>
#include <QVideoFrame>
#include <QVideoSink>
#include <QtConcurrent/QtConcurrent>
extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/imgutils.h>
#include <libswscale/swscale.h>
}
// todo implement std::priority_queue with thread pool

QSemaphore PhotoModel::m_videoThumbnailSemaphore(4);

PhotoModel::PhotoModel(iDescriptorDevice *device, FilterType filterType,
                       QObject *parent)
    : QAbstractListModel(parent), m_device(device), m_thumbnailSize(120, 120),
      m_sortOrder(NewestFirst), m_filterType(filterType)
{
    // 350 MB cache for thumbnails
    m_thumbnailCache.setMaxCost(350 * 1024 * 1024);

    connect(this, &PhotoModel::thumbnailNeedsToBeLoaded, this,
            &PhotoModel::requestThumbnail, Qt::QueuedConnection);
}

void PhotoModel::clear()
{
    blockSignals(true);

    // Clean up any active watchers
    for (auto *watcher : m_activeLoaders.values()) {
        if (watcher) {
            watcher->disconnect();
            watcher->cancel();
            // watcher->waitForFinished();
            watcher->deleteLater();
        }
    }
    m_activeLoaders.clear();
    m_loadingPaths.clear();
    m_thumbnailCache.clear();

    beginResetModel();
    m_photos.clear();
    m_allPhotos.clear();
    endResetModel();

    blockSignals(false);
}

PhotoModel::~PhotoModel()
{
    qDebug() << "PhotoModel destructor called";
    clear();
}

QPixmap PhotoModel::generateVideoThumbnailFFmpeg(iDescriptorDevice *device,
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
        iDescriptorDevice *device;
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

int PhotoModel::rowCount(const QModelIndex &parent) const
{
    Q_UNUSED(parent)
    return m_photos.size();
}

QVariant PhotoModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= m_photos.size())
        return QVariant();

    const PhotoInfo &info = m_photos.at(index.row());

    switch (role) {
    case Qt::DisplayRole:
        return info.fileName;

    case Qt::UserRole:
        return info.filePath;

    case Qt::DecorationRole: {

        // Check memory cache first
        if (QPixmap *cached = m_thumbnailCache.object(info.filePath)) {
            return QIcon(*cached);
        }

        // Prevent duplicate requests
        if (m_loadingPaths.contains(info.filePath) ||
            m_activeLoaders.contains(info.filePath)) {
            // Return appropriate placeholder based on file type
            if (info.fileName.endsWith(".MOV", Qt::CaseInsensitive) ||
                info.fileName.endsWith(".MP4", Qt::CaseInsensitive) ||
                info.fileName.endsWith(".M4V", Qt::CaseInsensitive)) {
                return QIcon(":/resources/icons/video-x-generic.png");
            } else {
                return QIcon(":/resources/icons/"
                             "MaterialSymbolsLightImageOutlineSharp.png");
            }
        }

        // Start async loading for both images and videos
        if (!m_loadingPaths.contains(info.filePath)) {
            emit const_cast<PhotoModel *>(this)->thumbnailNeedsToBeLoaded(
                index.row());
        }

        // Return placeholder while loading
        if (info.fileName.endsWith(".MOV", Qt::CaseInsensitive) ||
            info.fileName.endsWith(".MP4", Qt::CaseInsensitive) ||
            info.fileName.endsWith(".M4V", Qt::CaseInsensitive)) {
            // return QIcon::fromTheme("video-x-generic");
            return QIcon(":/resources/icons/video-x-generic.png");
        } else {
            return QIcon(
                ":/resources/icons/MaterialSymbolsLightImageOutlineSharp.png");
        }
    }

    case Qt::ToolTipRole:
        return QString("Photo: %1").arg(info.fileName);

    default:
        return QVariant();
    }
}

void PhotoModel::requestThumbnail(int index)
{
    if (index < 0 || index >= m_photos.size())
        return;

    PhotoInfo &info = m_photos[index];
    info.thumbnailRequested = true;

    if (m_loadingPaths.contains(info.filePath))
        return;

    m_loadingPaths.insert(info.filePath);

    auto *watcher = new QFutureWatcher<QPixmap>();
    m_activeLoaders[info.filePath] = watcher;

    connect(watcher, &QFutureWatcher<QPixmap>::finished, this,
            [this, watcher, filePath = info.filePath]() {
                QPixmap thumbnail = watcher->result();

                m_loadingPaths.remove(filePath);
                m_activeLoaders.remove(filePath);
                if (!thumbnail.isNull()) {
                    int cost = thumbnail.width() * thumbnail.height() * 4;
                    m_thumbnailCache.insert(filePath, new QPixmap(thumbnail),
                                            cost);

                    for (int i = 0; i < m_photos.size(); ++i) {
                        if (m_photos[i].filePath == filePath) {
                            QModelIndex idx = createIndex(i, 0);
                            emit dataChanged(idx, idx, {Qt::DecorationRole});
                            break;
                        }
                    }
                } else {
                    qDebug() << "Failed to load thumbnail for:"
                             << QFileInfo(filePath).fileName();
                }

                watcher->deleteLater();
            });

    bool isVideo = info.fileName.endsWith(".MOV", Qt::CaseInsensitive) ||
                   info.fileName.endsWith(".MP4", Qt::CaseInsensitive) ||
                   info.fileName.endsWith(".M4V", Qt::CaseInsensitive);

    QFuture<QPixmap> future;
    if (isVideo) {
        future = QtConcurrent::run([this, info]() {
            // Acquire semaphore FIRST to limit concurrent video processing
            m_videoThumbnailSemaphore.acquire();

            // Generate video thumbnail using FFmpeg directly (no QMediaPlayer)
            QPixmap thumbnail = generateVideoThumbnailFFmpeg(
                m_device, info.filePath, m_thumbnailSize);

            m_videoThumbnailSemaphore.release();
            return thumbnail;
        });
    } else {
        future = QtConcurrent::run([info, this]() {
            return loadThumbnailFromDevice(m_device, info.filePath,
                                           m_thumbnailSize);
        });
    }

    watcher->setFuture(future);
}

// Static function that runs in worker thread
QPixmap PhotoModel::loadThumbnailFromDevice(iDescriptorDevice *device,
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

QPixmap PhotoModel::loadImage(iDescriptorDevice *device,
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

void PhotoModel::populatePhotoPaths()
{
    // TODO:beginResetModel called on PhotoModel(0x600002d12a40) without calling
    // endResetModel first
    if (m_albumPath.isEmpty()) {
        qDebug() << "No album path set, skipping population";
        return;
    }

    m_allPhotos.clear();

    QByteArray albumPathBytes = m_albumPath.toUtf8();
    const char *albumPathCStr = albumPathBytes.constData();

    AfcFileInfo albumInfo = {};

    IdeviceFfiError *err =
        ServiceManager::safeAfcGetFileInfo(m_device, albumPathCStr, &albumInfo);
    if (err) {
        qDebug() << "Album path does not exist or cannot be accessed:"
                 << m_albumPath << "Error:" << err->message;
        idevice_error_free(err);
        return;
    }
    // FIXME: should we continue if albumInfo is null?
    if (albumInfo.size) {
        afc_file_info_free(&albumInfo);
    }

    // Fix: Store the QByteArray to keep the C string valid
    QByteArray photoDirBytes = m_albumPath.toUtf8();
    const char *photoDir = photoDirBytes.constData();
    qDebug() << "Photo directory:" << m_albumPath;
    qDebug() << "Photo directory C string:" << photoDir;

    char **files = nullptr;
    size_t count = 0;
    err =
        ServiceManager::safeAfcReadDirectory(m_device, photoDir, &files, count);
    if (err) {
        qDebug() << "Failed to read photo directory:" << photoDir
                 << "Error:" << err->message;
        idevice_error_free(err);
        return;
    }

    if (files) {
        for (int i = 0; files[i]; i++) {
            QString fileName = QString::fromUtf8(files[i]);
            if (fileName.endsWith(".JPG", Qt::CaseInsensitive) ||
                fileName.endsWith(".PNG", Qt::CaseInsensitive) ||
                fileName.endsWith(".HEIC", Qt::CaseInsensitive) ||
                fileName.endsWith(".MOV", Qt::CaseInsensitive) ||
                fileName.endsWith(".MP4", Qt::CaseInsensitive) ||
                fileName.endsWith(".M4V", Qt::CaseInsensitive)) {

                PhotoInfo info;
                info.filePath = m_albumPath + "/" + fileName;
                info.fileName = fileName;
                info.thumbnailRequested = false;
                info.fileType = determineFileType(fileName);
                info.dateTime = extractDateTimeFromFile(info.filePath);

                m_allPhotos.append(info);
            }
        }
        free_directory_listing(files, count);
    }

    // Apply initial filtering and sorting, which will also reset the model
    applyFilterAndSort();

    qDebug() << "Loaded" << m_allPhotos.size() << "media files from device";
    qDebug() << "After filtering:" << m_photos.size() << "items shown";
}

// Sorting and filtering methods
void PhotoModel::setSortOrder(SortOrder order)
{
    if (m_sortOrder != order) {
        m_sortOrder = order;
        applyFilterAndSort();
    }
}

void PhotoModel::setFilterType(FilterType filter)
{
    if (m_filterType != filter) {
        m_filterType = filter;
        applyFilterAndSort();
    }
}

void PhotoModel::applyFilterAndSort()
{
    beginResetModel();

    // Filter photos
    m_photos.clear();
    for (const PhotoInfo &info : m_allPhotos) {
        if (matchesFilter(info)) {
            m_photos.append(info);
        }
    }

    // Sort photos
    sortPhotos(m_photos);

    endResetModel();

    qDebug() << "Applied filter and sort - showing" << m_photos.size() << "of"
             << m_allPhotos.size() << "items";
}

void PhotoModel::sortPhotos(QList<PhotoInfo> &photos) const
{
    std::sort(photos.begin(), photos.end(),
              [this](const PhotoInfo &a, const PhotoInfo &b) {
                  if (m_sortOrder == NewestFirst) {
                      return a.dateTime > b.dateTime;
                  } else {
                      return a.dateTime < b.dateTime;
                  }
              });
}

bool PhotoModel::matchesFilter(const PhotoInfo &info) const
{
    switch (m_filterType) {
    case All:
        return true;
    case ImagesOnly:
        return info.fileType == PhotoInfo::Image;
    case VideosOnly:
        return info.fileType == PhotoInfo::Video;
    default:
        return true;
    }
}

// Export functionality
QStringList
PhotoModel::getSelectedFilePaths(const QModelIndexList &indexes) const
{
    QStringList paths;
    for (const QModelIndex &index : indexes) {
        if (index.isValid() && index.row() < m_photos.size()) {
            paths.append(m_photos.at(index.row()).filePath);
        }
    }
    return paths;
}

QString PhotoModel::getFilePath(const QModelIndex &index) const
{
    if (index.isValid() && index.row() < m_photos.size()) {
        return m_photos.at(index.row()).filePath;
    }
    return QString();
}

PhotoInfo::FileType PhotoModel::getFileType(const QModelIndex &index) const
{
    if (index.isValid() && index.row() < m_photos.size()) {
        return m_photos.at(index.row()).fileType;
    }
    return PhotoInfo::Image;
}

QStringList PhotoModel::getAllFilePaths() const
{
    QStringList paths;
    for (const PhotoInfo &info : m_allPhotos) {
        paths.append(info.filePath);
    }
    return paths;
}

QStringList PhotoModel::getFilteredFilePaths() const
{
    QStringList paths;
    for (const PhotoInfo &info : m_photos) {
        paths.append(info.filePath);
    }
    return paths;
}

// Helper methods
QDateTime PhotoModel::extractDateTimeFromFile(const QString &filePath) const
{
    AfcFileInfo *info = nullptr;
    IdeviceFfiError *err = ServiceManager::safeAfcGetFileInfo(
        m_device, filePath.toUtf8().constData(), info);
    if (!err && info) {
        uint64_t creation_seconds = info->creation;
        QDateTime dateTime =
            QDateTime::fromSecsSinceEpoch(creation_seconds, Qt::UTC);

        // afc_file_info_free(info);
        if (dateTime.isValid()) {
            return dateTime;
        }
    }

    return QDateTime::currentDateTime();
}

PhotoInfo::FileType PhotoModel::determineFileType(const QString &fileName) const
{
    if (fileName.endsWith(".MOV", Qt::CaseInsensitive) ||
        fileName.endsWith(".MP4", Qt::CaseInsensitive) ||
        fileName.endsWith(".M4V", Qt::CaseInsensitive)) {
        return PhotoInfo::Video;
    } else {
        return PhotoInfo::Image;
    }
}

void PhotoModel::setAlbumPath(const QString &albumPath)
{
    qDebug() << "Setting new album path:" << albumPath;
    clear();

    m_albumPath = albumPath;
    populatePhotoPaths();
}

void PhotoModel::refreshPhotos() { populatePhotoPaths(); }