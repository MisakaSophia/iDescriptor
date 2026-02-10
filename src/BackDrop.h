// C++ port of https://github.com/GvozdevLeonid/BackDrop-in-PyQt-PySide

#ifndef BACKDROP_H
#define BACKDROP_H

#include <QBrush>
#include <QColor>
#include <QEvent>
#include <QGraphicsEffect>
#include <QLinearGradient>
#include <QPixmap>
#include <QPointF>
#include <QPropertyAnimation>
#include <QSize>
#include <QVBoxLayout>
#include <QWidget>
#include <vector>

// ── Background layer description ────────────────────────────────────
struct BackgroundLayer {
    QBrush backgroundColor{QColor(255, 255, 255, 0)};
    QColor border{255, 255, 255, 0};
    int borderWidth = 1;
    qreal opacity = 0.0;
};

// ── BackDrop – a QGraphicsEffect that blurs what is behind the widget ──
class BackDrop : public QGraphicsEffect
{
    Q_OBJECT
    Q_PROPERTY(QPointF animationPosition READ animationPosition WRITE
                   setAnimationPosition)

public:
    explicit BackDrop(int blur = 0, int radius = 0,
                      const std::vector<BackgroundLayer> &backgrounds = {},
                      QObject *parent = nullptr);

    QPointF animationPosition() const;
    void setAnimationPosition(const QPointF &pos);

    void shineAnimation(int duration = 300, bool forward = true,
                        int angle = 135, int width = 40,
                        QColor color = QColor(255, 255, 255, 125));
    static QPixmap blurPixmap(const QPixmap &src, int blurRadius);
    static void cutPixmap(QPixmap &pixmap, const QPixmap &mask, int w, int h);
    static QPixmap getColoredPixmap(const QBrush &brushColor,
                                    const QColor &penColor, int penWidth, int w,
                                    int h, int radius);
    QPixmap getBlurBackground(const QPixmap &source);
    void createAnimationPixmap(int angle, int lineWidth, const QColor &color);

protected:
    void draw(QPainter *painter) override;

private:
    int m_blur;
    int m_radius;
    std::vector<BackgroundLayer> m_backgrounds;

    QSize m_size{0, 0};

    QPixmap m_animationPixmap;
    bool m_forwardAnimation = false;
    QPointF m_animationPosition{0, 0};
    QPropertyAnimation m_animation;
};

// ── BackDropWrapper – wraps any widget and adds backdrop + hover anims ──
class BackDropWrapper : public QWidget
{
    Q_OBJECT

public:
    explicit BackDropWrapper(
        QWidget *widget, int blur = 0, int radius = 0,
        const std::vector<BackgroundLayer> &backgrounds = {},
        QWidget *parent = nullptr);

    void enableShineAnimation(int duration = 300, bool forward = true,
                              int angle = 135, int width = 40,
                              QColor color = QColor(255, 255, 255, 125));

    void enableMoveAnimation(int duration = 300, QPointF offset = QPointF(0, 0),
                             bool forward = true);

protected:
    bool event(QEvent *event) override;

private:
    void moveAnimation(int duration, const QPointF &offset, bool forward);

    QWidget *m_widget = nullptr;
    BackDrop *m_backdrop = nullptr;

    QPropertyAnimation m_moveAnim;
    QPointF m_normalPos;
    bool m_moveForward = false;
    bool m_hasNormalPos = false;

    // Stored animation parameters
    struct ShineInfo {
        int duration = 300;
        bool forward = true;
        int angle = 135;
        int width = 40;
        QColor color{255, 255, 255, 125};
    };
    struct MoveInfo {
        int duration = 300;
        QPointF offset{0, 0};
        bool forward = true;
    };

    bool m_shineEnabled = false;
    ShineInfo m_shineInfo;

    bool m_moveEnabled = false;
    MoveInfo m_moveInfo;
};

#endif // BACKDROP_H
