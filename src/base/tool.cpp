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

#include "tool.h"
#ifdef WIN32
#include "../platform/windows/widgets/wintoolwidget.h"
#include "../platform/windows/win_common.h"
#endif

Tool::Tool(QWidget *parent, bool noMaximize) : QWidget(parent)
{
#ifdef __APPLE__
    setupToolFrame(this);
    if (noMaximize) {
        setWindowFlags(Qt::Window | Qt::WindowTitleHint |
                       Qt::CustomizeWindowHint | Qt::WindowCloseButtonHint |
                       Qt::WindowMinimizeButtonHint);
    } else {
        setWindowFlags(Qt::Window | Qt::WindowTitleHint |
                       Qt::CustomizeWindowHint | Qt::WindowCloseButtonHint |
                       Qt::WindowMinimizeButtonHint |
                       Qt::WindowMaximizeButtonHint);
    }
#elif defined(WIN32)
    setupWinWindow(this);
    if (noMaximize) {
        setWindowFlags(Qt::CustomizeWindowHint | Qt::WindowTitleHint |
                       Qt::WindowMinimizeButtonHint |
                       Qt::WindowCloseButtonHint);
    } else {
        setWindowFlags(Qt::CustomizeWindowHint | Qt::WindowTitleHint |
                       Qt::WindowMinimizeButtonHint |
                       Qt::WindowCloseButtonHint |
                       Qt::WindowMaximizeButtonHint);
    }
#else

#endif
}
