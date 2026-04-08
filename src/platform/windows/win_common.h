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
#include <QOperatingSystemVersion>
#include <QString>
#include <QWidget>
#include <dwmapi.h>
#include <ntstatus.h>
#include <windows.h>
#include <winsock2.h>
#include <winternl.h>
#include <ws2tcpip.h>

#include "../../service.h"

enum WIN_BACKDROP { MICA = 2, ACRYLIC = 3, MICA_ALT = 4 };

SERVICE_AVAILABILITY IsAppleMobileDeviceSupportInstalled();
SERVICE_AVAILABILITY IsWinFspInstalled();
bool is_iDescriptorInstalled();
SERVICE_AVAILABILITY IsBonjourServiceInstalled();

bool StartAppleMobileDeviceService();
bool StartWinFspService();
bool StartBonjourService();

void enableAcrylic(HWND hwnd);
void enableMica(HWND hwnd);

inline QString getWindowsAccentColor()
{
    HKEY hKey = nullptr;
    DWORD value = 0;
    DWORD size = sizeof(value);

    if (RegOpenKeyExW(
            HKEY_CURRENT_USER,
            L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Explorer\\Accent",
            0, KEY_READ, &hKey) == ERROR_SUCCESS) {
        const LONG res =
            RegQueryValueExW(hKey, L"AccentColorMenu", nullptr, nullptr,
                             reinterpret_cast<LPBYTE>(&value), &size);
        RegCloseKey(hKey);

        if (res == ERROR_SUCCESS) {
            // Value is stored as 0xAABBGGRR, convert to #RRGGBB
            const BYTE b = (value & 0x00FF0000) >> 16;
            const BYTE g = (value & 0x0000FF00) >> 8;
            const BYTE r = (value & 0x000000FF);

            return QString("#%1%2%3")
                .arg(r, 2, 16, QChar('0'))
                .arg(g, 2, 16, QChar('0'))
                .arg(b, 2, 16, QChar('0'))
                .toUpper();
        }
    }

    // Fallback default accent color
    return "#0078D4";
}

inline bool detectDarkModeWindows()
{
    HKEY hKey;
    DWORD value;
    DWORD size = sizeof(value);
    if (RegOpenKeyExW(HKEY_CURRENT_USER,
                      L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Themes\\P"
                      L"ersonalize",
                      0, KEY_READ, &hKey) == ERROR_SUCCESS) {
        if (RegQueryValueExW(hKey, L"AppsUseLightTheme", nullptr, nullptr,
                             reinterpret_cast<LPBYTE>(&value),
                             &size) == ERROR_SUCCESS) {
            RegCloseKey(hKey);
            return value == 0; // 0 means dark mode, 1 means light mode
        }
        RegCloseKey(hKey);
    }
    return true; // Default to dark mode if detection fails
}

inline void setupWinWindow(QWidget *window)
{
    QOperatingSystemVersion osVersion = QOperatingSystemVersion::current();
    if (osVersion < QOperatingSystemVersion::Windows11)
        return;
    window->setAttribute(Qt::WA_TranslucentBackground);
    HWND hwnd = reinterpret_cast<HWND>(window->winId());
    enableMica(hwnd);

    /*
        normally we had plans to enable acrylic on win 10 but since it's
       untested and may cause issues, we'll just enable mica on win 11 and above
       for now
    */
    // enableAcrylic(hwnd);
}

enum CornerPreference : int {
    Corner_Default = DWMWCP_DEFAULT,
    Corner_NoRound = DWMWCP_DONOTROUND,
    Corner_Round = DWMWCP_ROUND,
    Corner_RoundSmall = DWMWCP_ROUNDSMALL
};
/* apparently this only works on Win 11 but should not crash on older versions
 */
inline void SetCorner(HWND hwnd, CornerPreference corner)
{
    if (corner != Corner_Default) {
        DWM_WINDOW_CORNER_PREFERENCE cp =
            static_cast<DWM_WINDOW_CORNER_PREFERENCE>(corner);
        DwmSetWindowAttribute(hwnd, DWMWA_WINDOW_CORNER_PREFERENCE, &cp,
                              sizeof(cp));
    }
}
