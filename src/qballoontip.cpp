#include "qballoontip.h"
#include "iDescriptor.h"
#include <QApplication>
#include <QBasicTimer>
#include <QGraphicsDropShadowEffect>
#include <QGridLayout>
#include <QGuiApplication>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPropertyAnimation>
#include <QPushButton>
#include <QScreen>
#include <QStyle>
#include <QTimerEvent>
#include <qpainterpath.h>

void QBalloonTip::toggleBaloon(const QPoint &pos, int timeout,
                               bool forceVisible)
{
    if (m_visible && !forceVisible) {
        hideBalloon();
        return;
    }

    if (timeout < 0)
        timeout = 10000; // 10 s default
    balloon(pos, timeout);
}

void QBalloonTip::hideBalloon()
{
    m_visible = false;
    hide();
}

void QBalloonTip::updateBalloonPosition(const QPoint &pos)
{
    hideBalloon();
    balloon(pos, 0);
}

bool QBalloonTip::isBalloonVisible() { return m_visible; }

QBalloonTip::QBalloonTip(QWidget *widget)
    : QWidget(widget ? widget->window() : QApplication::activeWindow(),
              Qt::ToolTip),
      widget(widget)
{
    setObjectName("balloonTip");
#ifdef WIN32
    setAttribute(Qt::WA_TranslucentBackground);
#else
    setAttribute(Qt::WA_StyledBackground, true);
    setStyleSheet("QWidget#balloonTip { "
                  "  background-color: transparent;"
                  "}");
#endif

    if (widget) {
        connect(widget, &QWidget::destroyed, this, &QBalloonTip::close);
    } else if (QApplication::activeWindow()) {
        connect(QApplication::activeWindow(), &QWidget::destroyed, this,
                &QBalloonTip::close);
    }

    // Add drop shadow effect
    // QGraphicsDropShadowEffect *shadowEffect =
    //     new QGraphicsDropShadowEffect(this);
    // shadowEffect->setBlurRadius(200);
    // shadowEffect->setColor(QColor(0, 0, 0, 80));
    // shadowEffect->setOffset(0, 5);
    // setGraphicsEffect(shadowEffect);

    // QLabel *titleLabel = new QLabel;
    // titleLabel->installEventFilter(this);
    // titleLabel->setText(title);
    // QFont f = titleLabel->font();
    // f.setBold(true);
    // titleLabel->setFont(f);
    // titleLabel->setTextFormat(Qt::PlainText); // to maintain compat with
    // windows

    // const int iconSize = 18;
    // const int closeButtonSize = 15;

    // QPushButton *closeButton = new QPushButton;
    // closeButton->setIcon(style()->standardIcon(QStyle::SP_TitleBarCloseButton));
    // closeButton->setIconSize(QSize(closeButtonSize, closeButtonSize));
    // closeButton->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    // closeButton->setFixedSize(closeButtonSize, closeButtonSize);
    // QObject::connect(closeButton, SIGNAL(clicked()), this, SLOT(close()));

    // QLabel *msgLabel = new QLabel;
    // msgLabel->installEventFilter(this);
    // msgLabel->setText(message);
    // msgLabel->setTextFormat(Qt::PlainText);
    // msgLabel->setAlignment(Qt::AlignTop | Qt::AlignLeft);

    // QGridLayout *layout = new QGridLayout;
    // if (!icon.isNull()) {
    //     QLabel *iconLabel = new QLabel;
    //     iconLabel->setPixmap(
    //         icon.pixmap(QSize(iconSize, iconSize), devicePixelRatio()));
    //     iconLabel->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    //     iconLabel->setMargin(2);
    //     layout->addWidget(iconLabel, 0, 0);
    //     layout->addWidget(titleLabel, 0, 1);
    // } else {
    //     layout->addWidget(titleLabel, 0, 0, 1, 2);
    // }

    // layout->addWidget(closeButton, 0, 2);

    // layout->addWidget(msgLabel, 1, 0, 1, 3);
    // layout->setSizeConstraint(QLayout::SetFixedSize);
    // layout->setContentsMargins(3, 3, 3, 3);
    // setLayout(layout);
}

QBalloonTip::~QBalloonTip() {}

void QBalloonTip::paintEvent(QPaintEvent *ev)
{
    // QPainter painter(this);
    // painter.drawPixmap(rect(), pixmap);
    QWidget::paintEvent(ev);
}

void QBalloonTip::resizeEvent(QResizeEvent *ev) { QWidget::resizeEvent(ev); }

void QBalloonTip::balloon(const QPoint &pos, int msecs)
{
    m_visible = true;
    qApp->installEventFilter(this);

    QScreen *screen = QGuiApplication::screenAt(pos);
    if (!screen) {
        screen = QGuiApplication::primaryScreen();
    }
    QRect screenRect = screen->availableGeometry();

    // Ensure layout is up to date so we get the correct size
    ensurePolished();
    adjustSize();

    QSize balloonSize = size();
    if (!balloonSize.isValid() || balloonSize.isEmpty()) {
        balloonSize = sizeHint();
    }

    // Arrow dimensions
    const int arrowGap = 40; // gap between balloon and anchor
    const int margin = 5;    // margin from screen edges

    // Calculate horizontal position - center on the anchor point
    int balloonX = pos.x() - balloonSize.width() / 2;

    // Clamp to screen bounds with margin
    balloonX = qBound(screenRect.left() + margin, balloonX,
                      screenRect.right() - balloonSize.width() - margin);

    // Calculate vertical position - prefer above the anchor point
    int balloonY;
    int spaceAbove = pos.y() - screenRect.top();

    if (spaceAbove >= balloonSize.height() + arrowGap + margin) {
        // Show above the anchor point (preferred)
        balloonY = pos.y() - balloonSize.height() - arrowGap;
    } else {
        // Not enough space above, show below
        balloonY = pos.y() + arrowGap;
    }

    balloonY = qBound(screenRect.top() + margin, balloonY,
                      screenRect.bottom() - balloonSize.height() - margin);

    setGeometry(balloonX, balloonY, balloonSize.width(), balloonSize.height());

    show();
    raise();
    activateWindow();
}

void QBalloonTip::mousePressEvent(QMouseEvent *e)
{
    if (e->button() == Qt::LeftButton) {
        emit messageClicked();
    }
}

void QBalloonTip::timerEvent(QTimerEvent *e)
{
    if (e->timerId() == timer.timerId()) {
        timer.stop();
        if (!underMouse())
            close();
        return;
    }
    QWidget::timerEvent(e);
}

bool QBalloonTip::eventFilter(QObject *obj, QEvent *event)
{
    if (event->type() == QEvent::MouseButtonPress) {
        QMouseEvent *mouseEvent = static_cast<QMouseEvent *>(event);

        if (m_button) {
            if (QWidget *clickedWidget = qobject_cast<QWidget *>(obj)) {
                if (clickedWidget == m_button ||
                    m_button->isAncestorOf(clickedWidget)) {
                    return false;
                }
            }
        }

        if (m_visible && !geometry().contains(mouseEvent->globalPos())) {
            m_visible = false;
            close();
            return true;
        }
    } else if (event->type() == QEvent::WindowDeactivate) {
        // Close when window loses focus
        if (obj == this) {
            m_visible = false;
            close();
            return false;
        }
    } else if (event->type() == QEvent::ApplicationDeactivate) {
        // App went to background → hide balloon
        if (m_visible) {
            m_visible = false;
            close();
        }
        return false; // let others handle it too
    }

    // handle macOS tab switch and Escape key handling
    else if (event->type() == QEvent::KeyPress) {
        QKeyEvent *keyEvent = static_cast<QKeyEvent *>(event);
#ifdef __APPLE__
        if (auto *ke = dynamic_cast<QKeyEvent *>(event)) {
            const Qt::KeyboardModifiers mods = ke->modifiers();
            if ((mods & (Qt::MetaModifier | Qt::ControlModifier))) {
                if (m_visible) {
                    m_visible = false;
                    close();
                    return true;
                }
            }
        }
#endif
        if (keyEvent->key() == Qt::Key_Escape) {
            if (m_visible) {
                m_visible = false;
                close();
                return true;
            }
        }
    }

    return QWidget::eventFilter(obj, event);
}

void QBalloonTip::hideEvent(QHideEvent *event)
{
    // Remove event filter when hiding
    qApp->removeEventFilter(this);
    QWidget::hideEvent(event);
}
