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

#include "settingswidget.h"
#include "mainwindow.h"
#include "settingsmanager.h"
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QFileDialog>
#include <QFrame>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QScrollArea>
#include <QSpinBox>
#include <QStandardPaths>
#include <QTimer>
#include <QVBoxLayout>

SettingsWidget::SettingsWidget(QWidget *parent) : QDialog{parent}
{
    setupUI();
    loadSettings();
    connectSignals();
    // due to scrollbar add 10px on windows
#ifdef WIN32
    resize(sizeHint().width() + 10, sizeHint().height());
#endif
}

void SettingsWidget::setupUI()
{
    auto *mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(15);

    // Create scroll area for the settings
    auto *scrollArea = new QScrollArea();
    auto *scrollWidget = new QWidget();
    auto *scrollLayout = new QVBoxLayout(scrollWidget);
    scrollLayout->setSpacing(35);
    scrollLayout->setContentsMargins(10, 10, 10, 10);

    // === GENERAL SETTINGS ===
    auto *generalGroup = new QGroupBox("General");
    auto *generalLayout = new QVBoxLayout(generalGroup);

    // Download path
    auto *downloadLayout = new QHBoxLayout();
    downloadLayout->addWidget(new QLabel("Download Path:"));
    m_downloadPathEdit = new QLineEdit();
    m_downloadPathEdit->setReadOnly(true);
    m_downloadPathEdit->setMaximumWidth(300);
    downloadLayout->addWidget(m_downloadPathEdit);
    auto *browseButton = new QPushButton("Browse...");
    downloadLayout->addWidget(browseButton);
    generalLayout->addLayout(downloadLayout);

    // Wireless file server port
    auto *portLayout = new QHBoxLayout();
    portLayout->addWidget(new QLabel("Wireless File Server Port:"));
    m_wirelessFileServerPort = new QSpinBox();
    m_wirelessFileServerPort->setRange(1024, 65535);
    m_wirelessFileServerPort->setToolTip(
        "The starting port for the wireless file server. If this port is "
        "unavailable, it will try the next 10 ports.");
    portLayout->addWidget(m_wirelessFileServerPort);
    portLayout->addStretch();
    generalLayout->addLayout(portLayout);

    // Unmount iFuse drives on exit (not implemented on macOS)
    // TODO: Implement
#ifndef __APPLE__
    m_unmount_iFuseDrives = new QCheckBox("Unmount iFuse drives on exit");
    generalLayout->addWidget(m_unmount_iFuseDrives);
#endif

    connect(browseButton, &QPushButton::clicked, this,
            &SettingsWidget::onBrowseButtonClicked);

    // Auto-check for updates
    m_autoUpdateCheck = new QCheckBox("Automatically check for updates");
    generalLayout->addWidget(m_autoUpdateCheck);

    // Theme selection
    auto *themeLayout = new QHBoxLayout();
    themeLayout->addWidget(new QLabel("Theme:"));
    m_themeCombo = new QComboBox();

    /* FIXME: Theme control needs to be implemented */
    m_themeCombo->addItems({"System Default"});
    // m_themeCombo->addItems({"System Default", "Light", "Dark"});

    themeLayout->addWidget(m_themeCombo);
    themeLayout->addStretch();
    generalLayout->addLayout(themeLayout);

    scrollLayout->addWidget(generalGroup);

    // === DEVICE CONNECTION SETTINGS ===
    auto *deviceGroup = new QGroupBox("Device Connection");
    auto *deviceLayout = new QVBoxLayout(deviceGroup);

    m_autoRaiseWindow =
        new QCheckBox("Auto-raise main window on device connection");
    deviceLayout->addWidget(m_autoRaiseWindow);

    m_switchToNewDevice = new QCheckBox("Switch to newly connected device");
    deviceLayout->addWidget(m_switchToNewDevice);

    // Connection timeout
    auto *timeoutLayout = new QHBoxLayout();
    timeoutLayout->addWidget(new QLabel("Connection Timeout:"));
    m_connectionTimeout = new QSpinBox();
    m_connectionTimeout->setRange(5, 60);
    m_connectionTimeout->setSuffix(" seconds");
    timeoutLayout->addWidget(m_connectionTimeout);
    timeoutLayout->addStretch();
    deviceLayout->addLayout(timeoutLayout);

    scrollLayout->addWidget(deviceGroup);

    // === SECURITY SETTINGS ===
    auto *securityGroup = new QGroupBox("Security");
    auto *securityLayout = new QVBoxLayout(securityGroup);

    m_useUnsecureBackend =
        new QCheckBox("Use unsecure backend for app store (ipatool)");
    m_useUnsecureBackend->setToolTip(
        "Enabling this may put your Apple account at risk but you don't have "
        "to deal with Apple keychain.");
    securityLayout->addWidget(m_useUnsecureBackend);
    scrollLayout->addWidget(securityGroup);

    // === JAILBROKEN SETTINGS ===
    auto *jailbrokenGroup = new QGroupBox("Jailbroken");
    auto *jailbrokenLayout = new QVBoxLayout(jailbrokenGroup);

    // Default jailbroken root password
    auto *passwordLayout = new QHBoxLayout();
    passwordLayout->addWidget(new QLabel("Default Jailbroken Root Password:"));
    m_defaultJailbrokenRootPassword = new QLineEdit();
    m_defaultJailbrokenRootPassword->setEchoMode(QLineEdit::PasswordEchoOnEdit);
    m_defaultJailbrokenRootPassword->setMaximumWidth(200);
    m_defaultJailbrokenRootPassword->setToolTip(
        "Default password used for SSH root authentication on jailbroken "
        "devices: Default is 'alpine'.");
    passwordLayout->addWidget(m_defaultJailbrokenRootPassword);
    passwordLayout->addStretch();
    jailbrokenLayout->addLayout(passwordLayout);

    scrollLayout->addWidget(jailbrokenGroup);

    // === AirPlay SETTINGS ===
    auto *airplayGroup = new QGroupBox("AirPlay");
    auto *airplayLayout = new QVBoxLayout(airplayGroup);

    auto *fpsLayout = new QHBoxLayout();

    auto *fpsLabel = new QLabel("Fps:");
    m_fpsComboBox = new QComboBox();
    m_fpsComboBox->addItems({"24", "30", "60", "120"});
    m_fpsComboBox->setToolTip(
        "Set the fps for AirPlay. Go with 30 fps if have an older device.");

    fpsLayout->addWidget(fpsLabel);
    fpsLayout->addWidget(m_fpsComboBox);
    fpsLayout->addStretch();
    airplayLayout->addLayout(fpsLayout);

    m_noHoldCheckbox = new QCheckBox("Allow New Connections to Take Over");
    airplayLayout->addWidget(m_noHoldCheckbox);


#ifdef __linux__
    m_showV4L2CheckBox = new QCheckBox("Show V4L2 Button on AirPlay Widget");
    airplayLayout->addWidget(m_showV4L2CheckBox);
#endif

    scrollLayout->addWidget(airplayGroup);

    // === MISCELLANEOUS SETTINGS ===
    auto *miscGroup = new QGroupBox("Miscellaneous");
    auto *miscLayout = new QVBoxLayout(miscGroup);

    auto *iconSizeBaseMultiplierLayout = new QHBoxLayout();
    m_iconSizeBaseMultiplier = new QDoubleSpinBox();
    m_iconSizeBaseMultiplier->setRange(1.0, 5.0);
    m_iconSizeBaseMultiplier->setSingleStep(0.1);
    m_iconSizeBaseMultiplier->setDecimals(1);
    m_iconSizeBaseMultiplier->setSuffix("x");
    m_iconSizeBaseMultiplier->setToolTip(
        "Adjust the base multiplier for icon sizes. This affects how large "
        "icons appear throughout the application. Requires restart to take "
        "effect.");

    iconSizeBaseMultiplierLayout->addWidget(
        new QLabel("Icon Size Base Multiplier:"));
    iconSizeBaseMultiplierLayout->addWidget(m_iconSizeBaseMultiplier);
    iconSizeBaseMultiplierLayout->addStretch();
    miscLayout->addLayout(iconSizeBaseMultiplierLayout);

    scrollLayout->addWidget(miscGroup);

    scrollLayout->addSpacing(30);

    // Add a footer Author & Version & app info & app description
    auto *footerLabel = new QLabel(
        QString(
            "iDescriptor v%1\n"
            "A free, open-source, and cross-platform iDevice management tool.\n"
            "© 2026 See AUTHORS for details. Licensed under AGPLv3.")
            .arg(APP_VERSION));
    footerLabel->setAlignment(Qt::AlignCenter);
    footerLabel->setStyleSheet("color: gray; font-size: 8pt;");
    scrollLayout->addWidget(footerLabel);

    // Add stretch to push everything to the top
    scrollLayout->addStretch();

    scrollArea->setWidget(scrollWidget);
    scrollArea->setWidgetResizable(true);
    scrollArea->setFrameStyle(QFrame::NoFrame);
    scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

    // == BUTTONS ===
    auto *buttonLayout = new QHBoxLayout();

    m_checkUpdatesButton = new QPushButton("Check for Updates");
    m_resetButton = new QPushButton("Reset Settings");
    m_applyButton = new QPushButton("Apply");

    buttonLayout->addWidget(m_checkUpdatesButton);
    buttonLayout->addWidget(m_resetButton);
    buttonLayout->addWidget(m_applyButton);
    buttonLayout->setContentsMargins(10, 10, 10, 10);

    mainLayout->addWidget(scrollArea);
    mainLayout->addLayout(buttonLayout);

    // Connect button signals
    connect(m_checkUpdatesButton, &QPushButton::clicked, this,
            &SettingsWidget::onCheckUpdatesClicked);
    connect(m_resetButton, &QPushButton::clicked, this,
            &SettingsWidget::onResetToDefaultsClicked);
    connect(m_applyButton, &QPushButton::clicked, this,
            &SettingsWidget::onApplyClicked);
}

void SettingsWidget::loadSettings()
{
    SettingsManager *sm = SettingsManager::sharedInstance();

    m_downloadPathEdit->setText(sm->devdiskimgpath());
    m_autoUpdateCheck->setChecked(sm->autoCheckUpdates());
    m_autoRaiseWindow->setChecked(sm->autoRaiseWindow());
    m_switchToNewDevice->setChecked(sm->switchToNewDevice());
    m_wirelessFileServerPort->setValue(sm->wirelessFileServerPort());

#ifndef __APPLE__
    m_unmount_iFuseDrives->setChecked(sm->unmountiFuseOnExit());
#endif

    // Set theme combo box
    QString currentTheme = sm->theme();
    int themeIndex = m_themeCombo->findText(currentTheme);
    if (themeIndex != -1) {
        m_themeCombo->setCurrentIndex(themeIndex);
    }

    m_connectionTimeout->setValue(sm->connectionTimeout());
    m_useUnsecureBackend->setChecked(sm->useUnsecureBackend());
    m_defaultJailbrokenRootPassword->setText(
        sm->defaultJailbrokenRootPassword());

    // Disable apply button initially
    m_applyButton->setEnabled(false);

    m_iconSizeBaseMultiplier->setValue(sm->iconSizeBaseMultiplier());
    m_fpsComboBox->setCurrentText(QString::number(sm->airplayFps()));
    m_noHoldCheckbox->setChecked(sm->airplayNoHold());
#ifdef __linux__
    m_showV4L2CheckBox->setChecked(sm->showV4L2());
#endif
}

void SettingsWidget::connectSignals()
{
    // Connect all checkboxes and combos for immediate feedback
    connect(m_autoUpdateCheck, &QCheckBox::toggled, this,
            &SettingsWidget::onSettingChanged);
    connect(m_autoRaiseWindow, &QCheckBox::toggled, this,
            &SettingsWidget::onSettingChanged);
    connect(m_switchToNewDevice, &QCheckBox::toggled, this,
            &SettingsWidget::onSettingChanged);
#ifndef __APPLE__
    connect(m_unmount_iFuseDrives, &QCheckBox::toggled, this,
            &SettingsWidget::onSettingChanged);
#endif
    connect(m_themeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &SettingsWidget::onSettingChanged);
    connect(m_connectionTimeout, QOverload<int>::of(&QSpinBox::valueChanged),
            this, &SettingsWidget::onSettingChanged);
    connect(m_wirelessFileServerPort,
            QOverload<int>::of(&QSpinBox::valueChanged), this,
            &SettingsWidget::onSettingChanged);

    connect(m_iconSizeBaseMultiplier,
            QOverload<double>::of(&QDoubleSpinBox::valueChanged), this,
            [this]() {
                m_restartRequired = true;
                onSettingChanged();
            });

    connect(m_useUnsecureBackend, &QCheckBox::toggled, this, [this]() {
        // since this is unsafe if its being enabled, show a warning
        if (m_useUnsecureBackend->isChecked()) {
            auto reply = QMessageBox::warning(
                this, "Warning",
                "Enabling this will not encrypt your Apple account which "
                "is a "
                "security risk. Are you sure you want to enable this?",
                QMessageBox::Yes | QMessageBox::No, QMessageBox::No);

            if (reply == QMessageBox::Yes) {
                m_restartRequired = true;
                onSettingChanged();
            } else {
                m_useUnsecureBackend->setChecked(false);
            }
        } else {
            m_restartRequired = true;
            onSettingChanged();
        }
    });

    connect(m_defaultJailbrokenRootPassword, &QLineEdit::textChanged, this,
            &SettingsWidget::onSettingChanged);
    connect(m_fpsComboBox, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            &SettingsWidget::onSettingChanged);
    connect(m_noHoldCheckbox, &QCheckBox::toggled, this,
            &SettingsWidget::onSettingChanged);
#ifdef __linux__
    connect(m_showV4L2CheckBox, &QCheckBox::toggled, this,
            &SettingsWidget::onSettingChanged);
#endif
}

void SettingsWidget::onBrowseButtonClicked()
{
    QString dir = QFileDialog::getExistingDirectory(
        this, "Select Download Directory", m_downloadPathEdit->text(),
        QFileDialog::ShowDirsOnly);

    if (!dir.isEmpty()) {
        m_downloadPathEdit->setText(dir);
        onSettingChanged();
    }
}

void SettingsWidget::onCheckUpdatesClicked()
{
    m_checkUpdatesButton->setText("Checking...");
    m_checkUpdatesButton->setEnabled(false);

    connect(
        MainWindow::sharedInstance()->m_updater, &ZUpdater::dataAvailable, this,
        [this](const QJsonDocument data, bool isUpdateAvailable) {
            if (!isUpdateAvailable) {
                QMessageBox::information(this, "No Updates",
                                         "You are using the latest version of "
                                         "iDescriptor.");
            }
            m_checkUpdatesButton->setText("Check for Updates");
            m_checkUpdatesButton->setEnabled(true);
        },
        Qt::SingleShotConnection);

    MainWindow::sharedInstance()->m_updater->checkForUpdates();
}

void SettingsWidget::onResetToDefaultsClicked()
{
    auto reply = QMessageBox::question(
        this, "Reset Settings",
        "Are you sure you want to reset all settings to their default values?",
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);

    if (reply == QMessageBox::Yes) {
        resetToDefaults();
    }
}

void SettingsWidget::onApplyClicked()
{
    saveSettings();
    QMessageBox::information(this, "Settings",
                             m_restartRequired
                                 ? "Settings applied. Please restart "
                                   "the application for changes to "
                                   "take effect."
                                 : "Settings applied.");
    m_restartRequired = false;
}

void SettingsWidget::onSettingChanged()
{
    // Enable apply button when settings change
    m_applyButton->setEnabled(true);
}

void SettingsWidget::saveSettings()
{
    SettingsManager *sm = SettingsManager::sharedInstance();

    sm->setDevDiskImgPath(m_downloadPathEdit->text());
    sm->setAutoCheckUpdates(m_autoUpdateCheck->isChecked());
    sm->setAutoRaiseWindow(m_autoRaiseWindow->isChecked());
    sm->setSwitchToNewDevice(m_switchToNewDevice->isChecked());
    sm->setWirelessFileServerPort(m_wirelessFileServerPort->value());

#ifndef __APPLE__
    sm->setUnmountiFuseOnExit(m_unmount_iFuseDrives->isChecked());
#endif
    sm->setUseUnsecureBackend(m_useUnsecureBackend->isChecked());

    sm->setTheme(m_themeCombo->currentText());
    sm->setConnectionTimeout(m_connectionTimeout->value());
    sm->setDefaultJailbrokenRootPassword(
        m_defaultJailbrokenRootPassword->text());

    sm->setIconSizeBaseMultiplier(m_iconSizeBaseMultiplier->value());

    sm->setAirplayFps(m_fpsComboBox->currentText().toInt());
    sm->setAirplayNoHold(m_noHoldCheckbox->isChecked());
#ifdef __linux__
    sm->setShowV4L2(m_showV4L2CheckBox->isChecked());
#endif
    m_applyButton->setEnabled(false);
}

void SettingsWidget::resetToDefaults()
{
    SettingsManager::sharedInstance()->resetToDefaults();

    // Reload UI with default values
    loadSettings();

    onSettingChanged();
}
