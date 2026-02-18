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
#include <QMainWindow>
#include <QPoint>
#include <QString>
#include <QWidget>

struct UsageInfo {
    QString type;
    QString formattedSize;
    QString color;
    double percentage;
};

void setupMacOSWindow(QMainWindow *window);

void showPopoverForBarWidget(QWidget *widget, const UsageInfo &info);

void hidePopoverForBarWidget();

void setupToolFrame(QWidget *parent);