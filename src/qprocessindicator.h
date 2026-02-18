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

// https://github.com/raythorn/QProcessIndicator/blob/master/QProcessIndicator/QProcessIndicator.h
#ifndef QPROCESSINDICATOR_H
#define QPROCESSINDICATOR_H

#include <QColor>
#include <QEvent>
#include <QPaintEvent>
#include <QPainter>
#include <QTimer>
#include <QWidget>

class QProcessIndicator : public QWidget
{
    Q_OBJECT

    Q_PROPERTY(int m_type READ type WRITE setType)
    Q_PROPERTY(QColor m_color READ color WRITE setColor)
    Q_PROPERTY(int m_interval READ interval WRITE setInterval)

public:
    QProcessIndicator(QWidget *parent = 0);
    ~QProcessIndicator();

    enum {
        line_rotate,
        line_scale,
        ball_rotate,
    };

    void paintEvent(QPaintEvent *e) override;

    void start();
    void stop();

    int type() { return m_type; }
    void setType(int type) { m_type = type; }

    QColor &color() { return m_color; }
    void setColor(QColor &color) { m_color = color; }

    int interval() { return m_interval; }
    void setInterval(int interval) { m_interval = interval; }

private slots:
    void onTimeout();

private:
    void drawRotateLine(QPainter *painter);
    void drawScaleLine(QPainter *painter);
    void drawRotateBall(QPainter *painter);
    void updateStyle();

private:
    int m_type;
    int m_interval;
    QColor m_color;

    int m_angle;
    qreal m_scale;

    QTimer *m_timer;

protected:
    void changeEvent(QEvent *event) override
    {
        if (event->type() == QEvent::PaletteChange) {
            updateStyle();
        }
        QWidget::changeEvent(event);
    };
};

#endif // QPROCESSINDICATOR_H