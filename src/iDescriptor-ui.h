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

#pragma once
#include "settingsmanager.h"
#include <QAbstractButton>
#include <QApplication>
#include <QEvent>
#include <QGraphicsView>
#include <QGuiApplication>
#include <QLabel>
#include <QMainWindow>
#include <QMouseEvent>
#include <QPainter>
#include <QScreen>
#include <QSlider>
#include <QSplitter>
#include <QSplitterHandle>
#include <QStyleOption>
#include <QWheelEvent>
#include <QWidget>

#ifdef __APPLE__
#include "./platform/macos.h"
#endif

#define COLOR_GREEN QColor(0, 180, 0)    // Green
#define COLOR_ORANGE QColor(255, 140, 0) // Orange
#define COLOR_RED QColor(255, 0, 0)      // Red
#define COLOR_BLUE QColor("#2b5693")
#define COLOR_ACCENT_BLUE QColor("#0b5ed7")
#define MIN_MAIN_WINDOW_SIZE QSize(900, 600)

class ResponsiveGraphicsView : public QGraphicsView
{
public:
    ResponsiveGraphicsView(QGraphicsScene *scene, QWidget *parent = nullptr)
        : QGraphicsView(scene, parent)
    {
    }

protected:
    void resizeEvent(QResizeEvent *event) override
    {
        if (scene() && !scene()->items().isEmpty()) {
            fitInView(scene()->itemsBoundingRect(), Qt::KeepAspectRatio);
        }
        QGraphicsView::resizeEvent(event);
    }
};

class ClickableWidget : public QWidget
{
    Q_OBJECT
public:
    using QWidget::QWidget;

signals:
    void clicked();

protected:
    void mouseReleaseEvent(QMouseEvent *event) override
    {
        if (event->button() == Qt::LeftButton &&
            rect().contains(event->pos())) {
            emit clicked();
        }
        QWidget::mouseReleaseEvent(event);
    }
};

class ZIcon : public QIcon
{
public:
    ZIcon() : QIcon() {}
    ZIcon(const QIcon &icon) : QIcon(icon) {}
    ZIcon(const QString &fileName) : QIcon(fileName) {}
    ZIcon(const QPixmap &pixmap) : QIcon(pixmap) {}

    void setThemable(bool themable) { m_themable = themable; }

    QPixmap getThemedPixmap(const QSize &logicalSize, const QPalette &palette,
                            qreal dpr = 1.0) const
    {
        QSize physical = logicalSize * dpr;

        QPixmap pixmap = QIcon::pixmap(physical);
        if (pixmap.isNull())
            return pixmap;

        pixmap.setDevicePixelRatio(dpr);

        if (!m_themable)
            return pixmap;

        // theme color
        QColor iconColor = palette.color(QPalette::WindowText);

        QPixmap colored(pixmap.size());
        colored.setDevicePixelRatio(dpr);
        colored.fill(Qt::transparent);

        QPainter p(&colored);
        p.setRenderHint(QPainter::Antialiasing);
        p.drawPixmap(0, 0, pixmap);
        p.setCompositionMode(QPainter::CompositionMode_SourceIn);
        p.fillRect(colored.rect(), iconColor);
        p.end();

        return colored;
    }

    void paint(QPainter *painter, const QRect &logicalRect,
               const QPalette &palette, qreal dpr = 1.0) const
    {
        QPixmap pm = getThemedPixmap(logicalRect.size(), palette, dpr);
        if (pm.isNull())
            return;

        painter->drawPixmap(logicalRect, pm);
    }

private:
    bool m_themable = true;
};

class ZIconWidget : public QAbstractButton
{
    Q_OBJECT
public:
    ZIconWidget(const QIcon &icon, const QString &tooltip = "",
                qreal iconSizeMultiplier = 1.0, QWidget *parent = nullptr)
        : QAbstractButton(parent), m_icon(icon),
          m_iconSizeMultiplier(iconSizeMultiplier)
    {
        if (!tooltip.isEmpty())
            setToolTip(tooltip);

        updateIconSize();
        setCursor(Qt::PointingHandCursor);
    }

    void setIcon(const ZIcon &icon)
    {
        m_icon = icon;
        update();
    }

    void setIconSizeMultiplier(qreal multiplier)
    {
        m_iconSizeMultiplier = multiplier;
        updateIconSize();
    }

protected:
    void paintEvent(QPaintEvent *event) override
    {
        Q_UNUSED(event);
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);

        if (underMouse() || isDown()) {
            QColor bg = palette().color(QPalette::Highlight);
            bg.setAlpha(isDown() ? 60 : 30);
            painter.setBrush(bg);
            painter.setPen(Qt::NoPen);
            painter.drawEllipse(rect().adjusted(2, 2, -2, -2));
        }

        QRect iconRect = rect();
        iconRect.setSize(m_iconSize);
        iconRect.moveCenter(rect().center());

        m_icon.paint(&painter, iconRect, palette(), devicePixelRatioF());
    }

    void changeEvent(QEvent *event) override
    {
        if (event->type() == QEvent::ApplicationFontChange) {
            updateIconSize();
            /* TODO: may be use PaletteChange event?
             but ApplicationPaletteChange seems to be a better fit here than
             PaletteChange*/
        } else if (event->type() == QEvent::ApplicationPaletteChange) {
            update();
        }
        QAbstractButton::changeEvent(event);
    }

private:
    void updateIconSize()
    {
        QFontMetrics fm(font());
        int base =
            qRound(fm.height() * m_iconSizeMultiplier *
                   SettingsManager::sharedInstance()->iconSizeBaseMultiplier());

        m_iconSize = QSize(base, base);

        setFixedSize(base + 10, base + 10);

        update();
    }

private:
    ZIcon m_icon;
    QSize m_iconSize;
    qreal m_iconSizeMultiplier;
};

class ZIconLabel : public QLabel
{
    Q_OBJECT
public:
    ZIconLabel(const QIcon &icon, const QString &tooltip,
               qreal iconSizeMultiplier = 1.0, QWidget *parent = nullptr)
        : QLabel(parent), m_icon(icon), m_iconSizeMultiplier(iconSizeMultiplier)
    {
        setToolTip(tooltip);
        updateIconSize();
    }
    void setIcon(const QIcon &icon)
    {
        m_icon = ZIcon(icon);
        update();
    }

    void setIconThemable(bool themable)
    {
        m_icon.setThemable(themable);
        update();
    }

    void setIconSizeMultiplier(qreal multiplier)
    {
        m_iconSizeMultiplier = multiplier;
        updateIconSize();
    }

protected:
    void paintEvent(QPaintEvent *event) override
    {
        Q_UNUSED(event)
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);

        QRect iconRect = rect();
        iconRect.setSize(m_iconSize);
        iconRect.moveCenter(rect().center());

        m_icon.paint(&painter, iconRect, palette(), devicePixelRatioF());
    }

    void changeEvent(QEvent *event) override
    {
        if (event->type() == QEvent::ApplicationFontChange) {
            updateIconSize();
        } else if (event->type() == QEvent::ApplicationPaletteChange) {
            update();
        }
        QLabel::changeEvent(event);
    }

private:
    void updateIconSize()
    {
        QFontMetrics fm(font());
        int base =
            qRound(fm.height() * m_iconSizeMultiplier *
                   SettingsManager::sharedInstance()->iconSizeBaseMultiplier());

        m_iconSize = QSize(base, base);

        setFixedSize(base + 10, base + 10);

        update();
    }

    ZIcon m_icon;
    QSize m_iconSize;
    qreal m_iconSizeMultiplier;
};

enum class iDescriptorTool {
    Airplayer,
    LiveScreen,
    MountDevImage,
    VirtualLocation,
    Restart,
    Shutdown,
    RecoveryMode,
    QueryMobileGestalt,
    DeveloperDiskImages,
    WirelessGalleryImport,
    CableInfoWidget,
    /*
        TODO: to be implemented
        TouchIdTest,
        FaceIdTest,
    */
    NetworkDevices,
    iFuse,
    Unknown
};

class ModernSplitterHandle : public QSplitterHandle
{
public:
    ModernSplitterHandle(Qt::Orientation orientation, QSplitter *parent)
        : QSplitterHandle(orientation, parent)
    {
    }

protected:
    void paintEvent(QPaintEvent *event) override
    {
        Q_UNUSED(event)

        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);

        // Draw fading left and right borders (no top/bottom)
        QColor borderColor = QApplication::palette().color(QPalette::Mid);

        // Create gradient for fading effect
        int fadeMargin = 20; // pixels to fade over
        int centerHeight = height() / 2;
        int fadeStart = fadeMargin;
        int fadeEnd = height() - fadeMargin;

        // Left border with fade
        for (int y = 0; y < height(); ++y) {
            QColor currentColor = borderColor;
            if (y < fadeStart) {
                // Fade from transparent to full opacity
                float alpha = static_cast<float>(y) / fadeStart;
                currentColor.setAlphaF(alpha * borderColor.alphaF());
            } else if (y > fadeEnd) {
                // Fade from full opacity to transparent
                float alpha = static_cast<float>(height() - y) / fadeMargin;
                currentColor.setAlphaF(alpha * borderColor.alphaF());
            }
            painter.setPen(QPen(currentColor, 1));
            painter.drawPoint(0, y);
        }

        // Right border with fade
        for (int y = 0; y < height(); ++y) {
            QColor currentColor = borderColor;
            if (y < fadeStart) {
                float alpha = static_cast<float>(y) / fadeStart;
                currentColor.setAlphaF(alpha * borderColor.alphaF());
            } else if (y > fadeEnd) {
                float alpha = static_cast<float>(height() - y) / fadeMargin;
                currentColor.setAlphaF(alpha * borderColor.alphaF());
            }
            painter.setPen(QPen(currentColor, 1));
            painter.drawPoint(width() - 1, y);
        }

        // Draw the center button
        QColor buttonColor = QApplication::palette().color(QPalette::Text);
        buttonColor.setAlpha(60);

        int margin = 10;
        int availableWidth = width() - (2 * margin);
        int centerX = margin + availableWidth / 2;
        int centerY = height() / 2;

        int buttonWidth = 6;
        int buttonHeight = 50;

        QRect buttonRect(centerX - buttonWidth / 2, centerY - buttonHeight / 2,
                         buttonWidth, buttonHeight);

        painter.setBrush(QBrush(buttonColor));
        painter.setPen(Qt::NoPen);
        painter.drawRoundedRect(buttonRect, buttonWidth / 2, buttonWidth / 2);
    }
};

class ModernSplitter : public QSplitter
{
public:
    ModernSplitter(Qt::Orientation orientation, QWidget *parent = nullptr)
        : QSplitter(orientation, parent)
    {
        setHandleWidth(10);
    }

protected:
    QSplitterHandle *createHandle() override
    {
        return new ModernSplitterHandle(orientation(), this);
    }
};

class ZLabel : public QLabel
{
    Q_OBJECT
public:
    using QLabel::QLabel;

signals:
    void clicked();

protected:
    void mouseReleaseEvent(QMouseEvent *event) override
    {
        if (event->button() == Qt::LeftButton &&
            rect().contains(event->pos())) {
            emit clicked();
        }
        QLabel::mouseReleaseEvent(event);
    }
};

class ZSlider : public QSlider
{
    Q_OBJECT

public:
    explicit ZSlider(QWidget *parent = nullptr) : QSlider(parent) {}
    explicit ZSlider(Qt::Orientation orientation, QWidget *parent = nullptr)
        : QSlider(orientation, parent)
    {
    }

protected:
    void mousePressEvent(QMouseEvent *event) override
    {
        if (event->button() == Qt::LeftButton) {
            // Set the value to the position of the click
            int value = QStyle::sliderValueFromPosition(
                minimum(), maximum(), event->pos().x(), width());
            setValue(value);
        }
        // Let the base class handle the rest of the event
        QSlider::mousePressEvent(event);
    }
};