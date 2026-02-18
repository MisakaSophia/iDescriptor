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

#ifdef __APPLE__
#include "diskusagebar.h"
#include "platform/macos/macos.h"

#include <QEnterEvent>
#include <QHBoxLayout>
#include <QLabel>

DiskUsageBar::DiskUsageBar(QWidget *parent) : QWidget(parent), m_percentage(0.0)
{
    m_hoverTimer = new QTimer(this);
    m_hoverTimer->setSingleShot(true);
    m_hoverTimer->setInterval(500); // 500ms delay before showing popover
    connect(m_hoverTimer, &QTimer::timeout, this, &DiskUsageBar::showPopover);
    setAttribute(Qt::WA_Hover, true);

    QHBoxLayout *layout = new QHBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    // Add an invisible spacer to give the widget content
    QWidget *spacer = new QWidget(this);
    spacer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    layout->addWidget(spacer);

    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
}

void DiskUsageBar::setUsageInfo(const QString &type,
                                const QString &formattedSize,
                                const QString &color, double percentage)
{
    m_type = type;
    m_formattedSize = formattedSize;
    m_color = color;
    m_percentage = percentage;
}

void DiskUsageBar::enterEvent(QEnterEvent *event)
{
    Q_UNUSED(event);
    m_hoverTimer->start();
    QWidget::enterEvent(event);
}

void DiskUsageBar::leaveEvent(QEvent *event)
{
    m_hoverTimer->stop();
    hidePopoverForBarWidget();
    QWidget::leaveEvent(event);
}

void DiskUsageBar::showPopover()
{
    if (m_type.isEmpty())
        return;

    UsageInfo info;
    info.type = m_type;
    info.formattedSize = m_formattedSize;
    info.color = m_color;
    info.percentage = m_percentage;

    showPopoverForBarWidget(this, info);
}
#endif