// C++ port of https://github.com/GvozdevLeonid/BackDrop-in-PyQt-PySide

#include "BackDrop.h"

#include <QGraphicsBlurEffect>
#include <QGraphicsPixmapItem>
#include <QGraphicsScene>
#include <QImage>
#include <QPainter>
#include <QPen>
#include <QTransform>
#include <cmath>

// ═══════════════════════════════════════════════════════════════════════
//  BackDrop  (QGraphicsEffect)
// ═══════════════════════════════════════════════════════════════════════

BackDrop::BackDrop(int blur, int radius,
                   const std::vector<BackgroundLayer> &backgrounds,
                   QObject *parent)
    : QGraphicsEffect(parent), m_blur(blur), m_radius(radius),
      m_backgrounds(backgrounds), m_animation(this, "animationPosition")
{
}

QPointF BackDrop::animationPosition() const { return m_animationPosition; }

void BackDrop::setAnimationPosition(const QPointF &pos)
{
    m_animationPosition = pos;
    update();
}

// ── static helpers ──────────────────────────────────────────────────

QPixmap BackDrop::blurPixmap(const QPixmap &src, int blurRadius)
{
    int w = src.width();
    int h = src.height();

    auto *effect = new QGraphicsBlurEffect;
    effect->setBlurRadius(blurRadius);

    QGraphicsScene scene;
    auto *item = new QGraphicsPixmapItem;
    item->setPixmap(src);
    item->setGraphicsEffect(effect);
    scene.addItem(item);

    QImage res(QSize(w, h), QImage::Format_ARGB32);
    res.fill(Qt::transparent);

    QPainter ptr(&res);
    ptr.setRenderHints(QPainter::Antialiasing |
                       QPainter::SmoothPixmapTransform);
    scene.render(&ptr, QRectF(), QRectF(0, 0, w, h));
    ptr.end();

    return QPixmap::fromImage(res);
}

void BackDrop::cutPixmap(QPixmap &pixmap, const QPixmap &mask, int w, int h)
{
    QPainter painter(&pixmap);
    painter.setTransform(QTransform());
    painter.setRenderHints(QPainter::Antialiasing |
                           QPainter::SmoothPixmapTransform);
    painter.setCompositionMode(QPainter::CompositionMode_DestinationIn);
    painter.drawPixmap(0, 0, w, h, mask);
    painter.end();
}

QPixmap BackDrop::getColoredPixmap(const QBrush &brushColor,
                                   const QColor &penColor, int penWidth, int w,
                                   int h, int radius)
{
    QPixmap pixmap(w, h);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.setRenderHints(QPainter::Antialiasing |
                           QPainter::SmoothPixmapTransform);
    painter.setBrush(brushColor);
    painter.setPen(QPen(penColor, penWidth));
    painter.drawRoundedRect(QRectF(0.0, 0.0, w, h), radius, radius);
    painter.end();
    return pixmap;
}

QPixmap BackDrop::getBlurBackground(const QPixmap &devicePixmap)
{
    QRectF srcBound = sourceBoundingRect(Qt::DeviceCoordinates);
    QRect sourceRect = boundingRectFor(srcBound).toRect();
    int x = sourceRect.x();
    int y = sourceRect.y();
    int w = sourceRect.width();
    int h = sourceRect.height();
    int scale = static_cast<int>(devicePixmap.devicePixelRatioF());

    QPixmap bgExpanded =
        devicePixmap.copy(x * scale - m_blur, y * scale - m_blur,
                          w * scale + m_blur * 2, h * scale + m_blur * 2);

    QPixmap blurredExpanded = blurPixmap(bgExpanded, m_blur);

    QPixmap blurredBg = blurredExpanded.copy(m_blur / 2, m_blur / 2, w, h);
    return blurredBg;
}

// ── draw ────────────────────────────────────────────────────────────

void BackDrop::draw(QPainter *painter)
{
    painter->setRenderHints(QPainter::Antialiasing |
                            QPainter::SmoothPixmapTransform);
    QTransform restoreTransform = painter->worldTransform();

    QRectF srcBound = sourceBoundingRect(Qt::DeviceCoordinates);
    QRect sourceRect = boundingRectFor(srcBound).toRect();
    int x, y, w, h;
    sourceRect.getRect(&x, &y, &w, &h);

    QPoint offset;
    QPixmap source = sourcePixmap(Qt::DeviceCoordinates, &offset);
    int scale = static_cast<int>(source.devicePixelRatioF());

    if (m_size.width() != w || m_size.height() != h)
        m_size = QSize(w, h);

    // Get the blurred background from what is behind the widget
    QPixmap device = painter->device()
                         ? QPixmap::fromImage(
                               dynamic_cast<QImage *>(painter->device())
                                   ? *dynamic_cast<QImage *>(painter->device())
                                   : QImage())
                         : QPixmap();

    // We grab the background from the painter's device if possible.
    // If painter->device() is a QPixmap we use it directly.
    QPixmap devicePm;
    if (auto *pm = dynamic_cast<QPixmap *>(painter->device()))
        devicePm = *pm;
    else if (auto *img = dynamic_cast<QImage *>(painter->device()))
        devicePm = QPixmap::fromImage(*img);

    QPixmap mainBackground;
    if (!devicePm.isNull())
        mainBackground = getBlurBackground(devicePm);

    painter->setTransform(QTransform());
    painter->setPen(Qt::NoPen);

    // use consistent scaled sizes
    int sw = w * scale;
    int sh = h * scale;

    QPixmap mask = getColoredPixmap(QColor("#FFFFFF"), QColor("#FFFFFF"), 1, sw,
                                    sh, m_radius * scale);

    if (!mainBackground.isNull()) {
        cutPixmap(mainBackground, mask, sw, sh);
        painter->drawPixmap(x, y, w, h, mainBackground);
    }

    // Draw colored background layers
    for (auto &bg : m_backgrounds) {
        QPixmap bgPixmap =
            getColoredPixmap(bg.backgroundColor, bg.border,
                             bg.borderWidth * scale, sw, sh, m_radius * scale);
        // ensure we cut with the same scaled size
        cutPixmap(bgPixmap, mask, sw, sh);
        painter->setOpacity(bg.opacity);
        painter->drawPixmap(x, y, w, h, bgPixmap);
    }

    // Draw the original widget on top
    cutPixmap(source, mask, sw, sh);
    painter->setOpacity(1.0);
    painter->drawPixmap(x, y, source);

    // Shine animation overlay
    // TODO: remove
    if (m_animation.state() == QPropertyAnimation::Running) {
        QPixmap animPm(w, h);
        animPm.fill(Qt::transparent);
        QPainter animPainter(&animPm);
        animPainter.setRenderHints(QPainter::Antialiasing |
                                   QPainter::SmoothPixmapTransform);
        animPainter.drawPixmap(static_cast<int>(m_animationPosition.x()),
                               static_cast<int>(m_animationPosition.y()),
                               m_animationPixmap.width() / 2,
                               m_animationPixmap.height() / 2,
                               m_animationPixmap);
        animPainter.end();
        cutPixmap(animPm, mask, sw, sh);
        painter->drawPixmap(x, y, w, h, animPm);
    }

    painter->setWorldTransform(restoreTransform);
    painter->end();
}

// TODO: remove
// ── shine animation ─────────────────────────────────────────────────

void BackDrop::createAnimationPixmap(int angle, int lineWidth,
                                     const QColor &color)
{
    int height = m_size.height();
    double radAngle = qDegreesToRadians(static_cast<double>(angle));
    double diagonal = height / std::sin(radAngle);
    int width = static_cast<int>(
        std::sqrt(diagonal * diagonal - static_cast<double>(height) * height));
    int offsetVal =
        static_cast<int>(std::sqrt(static_cast<double>(lineWidth) * lineWidth +
                                   static_cast<double>(lineWidth) * lineWidth));

    QPointF startPos, endPos;

    if (angle == 0 || angle == 180) {
        width = m_size.height();
        startPos = QPointF(0, height);
        endPos = QPointF(width * 2 + offsetVal * 2, height);
    } else if (angle == 90) {
        width = lineWidth;
        startPos = QPointF(width + offsetVal, 0);
        endPos = QPointF(width + offsetVal, height * 2);
    } else {
        if (angle < 90) {
            startPos = QPointF(offsetVal, height * 2);
            endPos = QPointF(width * 2 + offsetVal, 0);
        } else {
            startPos = QPointF(offsetVal, 0);
            endPos = QPointF(width * 2 + offsetVal, height * 2);
        }
    }

    m_animationPixmap = QPixmap(width * 2 + offsetVal * 2, height * 2);
    m_animationPixmap.fill(Qt::transparent);

    QPainter painter(&m_animationPixmap);
    painter.setRenderHints(QPainter::Antialiasing |
                           QPainter::SmoothPixmapTransform);
    painter.setPen(QPen(color, lineWidth * 2));
    painter.drawLine(startPos, endPos);
    painter.end();
}

// TODO: remove
void BackDrop::shineAnimation(int duration, bool forward, int angle, int width,
                              QColor color)
{
    if (m_animation.state() != QPropertyAnimation::Running) {
        createAnimationPixmap(angle, width, color);

        QPointF startPoint, endPoint;
        if (forward) {
            startPoint = QPointF(-m_animationPixmap.width() / 2.0, 0);
            endPoint = QPointF(m_size.width(), 0);
            m_forwardAnimation = true;
        } else {
            startPoint = QPointF(m_size.width(), 0);
            endPoint = QPointF(-m_animationPixmap.width() / 2.0, 0);
            m_forwardAnimation = false;
        }

        m_animation.setStartValue(startPoint);
        m_animation.setEndValue(endPoint);
        m_animation.setDuration(duration);
        m_animation.start();

    } else if (m_animation.state() == QPropertyAnimation::Running &&
               !m_forwardAnimation) {
        m_forwardAnimation = false;
        m_animation.stop();
        QPointF endPt(m_size.width(), 0);
        m_animation.setStartValue(m_animationPosition);
        m_animation.setEndValue(endPt);
        m_animation.setDuration(duration - m_animation.currentTime());
        m_animation.start();

    } else if (m_animation.state() == QPropertyAnimation::Running &&
               m_forwardAnimation) {
        m_forwardAnimation = false;
        m_animation.stop();
        QPointF endPt(-m_animationPixmap.width() / 2.0, 0);
        m_animation.setStartValue(m_animationPosition);
        m_animation.setEndValue(endPt);
        m_animation.setDuration(duration - m_animation.currentTime());
        m_animation.start();
    }
}

// ═══════════════════════════════════════════════════════════════════════
//  BackDropWrapper
// ═══════════════════════════════════════════════════════════════════════

BackDropWrapper::BackDropWrapper(
    QWidget *widget, int blur, int radius,
    const std::vector<BackgroundLayer> &backgrounds, QWidget *parent)
    : QWidget(parent), m_widget(widget), m_moveAnim(this, "pos")
{

    auto *lay = new QVBoxLayout(this);
    lay->setContentsMargins(0, 0, 0, 0);
    lay->addWidget(m_widget);

    m_backdrop = new BackDrop(blur, radius, backgrounds, this);
    setGraphicsEffect(m_backdrop);

    setAttribute(Qt::WA_Hover);
    setAttribute(Qt::WA_NoMousePropagation);
}

void BackDropWrapper::enableShineAnimation(int duration, bool forward,
                                           int angle, int width, QColor color)
{
    m_shineEnabled = true;
    m_shineInfo = {duration, forward, angle, width, color};
}

void BackDropWrapper::enableMoveAnimation(int duration, QPointF offset,
                                          bool forward)
{
    m_moveEnabled = true;
    m_moveInfo = {duration, offset, forward};
}

bool BackDropWrapper::event(QEvent *e)
{
    if (e->type() == QEvent::HoverEnter) {
        if (m_shineEnabled) {
            m_backdrop->shineAnimation(m_shineInfo.duration,
                                       m_shineInfo.forward, m_shineInfo.angle,
                                       m_shineInfo.width, m_shineInfo.color);
        }
        if (m_moveEnabled) {
            moveAnimation(m_moveInfo.duration, m_moveInfo.offset,
                          m_moveInfo.forward);
        }
        return true;
    }
    if (e->type() == QEvent::HoverLeave) {
        if (m_shineEnabled) {
            m_backdrop->shineAnimation(m_shineInfo.duration,
                                       !m_shineInfo.forward, m_shineInfo.angle,
                                       m_shineInfo.width, m_shineInfo.color);
        }
        if (m_moveEnabled) {
            moveAnimation(m_moveInfo.duration, m_moveInfo.offset,
                          !m_moveInfo.forward);
        }
        return true;
    }
    return QWidget::event(e);
}

void BackDropWrapper::moveAnimation(int duration, const QPointF &offset,
                                    bool forward)
{
    if (!m_hasNormalPos) {
        m_normalPos = pos();
        m_hasNormalPos = true;
    }

    if (m_moveAnim.state() != QPropertyAnimation::Running) {
        if (forward) {
            m_normalPos = pos();
            m_moveForward = true;
            QPointF endPos(pos().x() + offset.x(), pos().y() + offset.y());
            m_moveAnim.setStartValue(pos());
            m_moveAnim.setEndValue(endPos);
        } else {
            m_moveForward = false;
            m_moveAnim.setStartValue(pos());
            m_moveAnim.setEndValue(m_normalPos);
        }
        m_moveAnim.setDuration(duration);
        m_moveAnim.start();

    } else if (m_moveAnim.state() == QPropertyAnimation::Running &&
               !m_moveForward) {
        m_moveForward = true;
        QPointF endPos(m_normalPos.x() + offset.x(),
                       m_normalPos.y() + offset.y());
        m_moveAnim.stop();
        m_moveAnim.setStartValue(m_moveAnim.currentValue().toPointF());
        m_moveAnim.setEndValue(endPos);
        m_moveAnim.setDuration(duration - m_moveAnim.currentTime());
        m_moveAnim.start();

    } else if (m_moveAnim.state() == QPropertyAnimation::Running &&
               m_moveForward) {
        m_moveForward = false;
        m_moveAnim.stop();
        m_moveAnim.setStartValue(m_moveAnim.currentValue().toPointF());
        m_moveAnim.setEndValue(m_normalPos);
        m_moveAnim.setDuration(duration - m_moveAnim.currentTime());
        m_moveAnim.start();
    }
}
