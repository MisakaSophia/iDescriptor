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

#ifndef SETTINGSWIDGET_H
#define SETTINGSWIDGET_H

#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QLineEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QWidget>

class SettingsWidget : public QDialog
{
    Q_OBJECT

public:
    explicit SettingsWidget(QWidget *parent = nullptr);

private slots:
    void onBrowseButtonClicked();
    void onCheckUpdatesClicked();
    void onResetToDefaultsClicked();
    void onApplyClicked();
    void onSettingChanged();

private:
    void setupUI();
    void loadSettings();
    void saveSettings();
    void connectSignals();
    void resetToDefaults();

    // UI Elements
    // General
    QLineEdit *m_downloadPathEdit;
    QSpinBox *m_wirelessFileServerPort;
    QCheckBox *m_autoUpdateCheck;
    QComboBox *m_themeCombo;
    QCheckBox *m_autoRaiseWindow;
    QCheckBox *m_switchToNewDevice;
#ifndef __APPLE__
    QCheckBox *m_unmount_iFuseDrives;
#endif
    QCheckBox *m_useUnsecureBackend;
    // Device Connection
    QCheckBox *m_autoConnectWirelessDevices;
    QSpinBox *m_connectionTimeout;

    // Jailbroken
    QLineEdit *m_defaultJailbrokenRootPassword;

    QDoubleSpinBox *m_iconSizeBaseMultiplier;

    // Airplay
    QComboBox *m_fpsComboBox;
    QCheckBox *m_noHoldCheckbox;

#ifdef __linux__
    QCheckBox *m_showV4L2CheckBox;
#endif

#ifdef WIN32
    QComboBox *m_backDropTypeCombo;
#endif

    // Buttons
    QPushButton *m_checkUpdatesButton;
    QPushButton *m_resetButton;
    QPushButton *m_applyButton;

    bool m_restartRequired = false;
};

#endif // SETTINGSWIDGET_H
