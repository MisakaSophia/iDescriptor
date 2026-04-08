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

#include "appinstalldialog.h"

AppInstallDialog::AppInstallDialog(const QString &appName,
                                   const QString &description,
                                   const QString &bundleId, QWidget *parent)
    : AppDownloadBaseDialog(appName, bundleId, parent), m_bundleId(bundleId),
      m_statusLabel(nullptr), m_installWatcher(nullptr)
{
    setWindowTitle("Install " + appName + " - iDescriptor");
    setModal(true);
    setFixedWidth(500);
    setContentsMargins(0, 0, 0, 0);

    QVBoxLayout *baseLayout = qobject_cast<QVBoxLayout *>(this->layout());
    m_loadingWidget = new ZLoadingWidget(false, this);
    baseLayout->addWidget(m_loadingWidget);

    QVBoxLayout *contentLayout = new QVBoxLayout();
    contentLayout->setContentsMargins(0, 0, 0, 0);

    // App info section
    QHBoxLayout *appInfoLayout = new QHBoxLayout();
    m_iconLabel = new IDLoadingIconLabel();
    QPointer<AppInstallDialog> safeThis = this;

    ::fetchAppIconFromApple(
        m_manager, bundleId,
        [safeThis](const QPixmap &pixmap, const QJsonObject &appInfo) {
            if (safeThis && safeThis.data()) {
                safeThis->m_iconLabel->setLoadedPixmap(pixmap);
            }
            if (safeThis && safeThis->m_loadingWidget) {
                safeThis->m_loadingWidget->stop(true);
            }
        });

    appInfoLayout->addWidget(m_iconLabel);

    QVBoxLayout *detailsLayout = new QVBoxLayout();
    QLabel *nameLabel = new QLabel(appName);
    {
        QFont f = nameLabel->font();
        f.setPointSize(20);
        f.setBold(true);
        nameLabel->setFont(f);
    }
    detailsLayout->addWidget(nameLabel);

    QLabel *descLabel = new QLabel(description);
    descLabel->setWordWrap(true);
    {
        QFont f = descLabel->font();
        f.setPointSize(14);
        descLabel->setFont(f);
    }
    detailsLayout->addWidget(descLabel);

    appInfoLayout->addLayout(detailsLayout);
    appInfoLayout->addStretch();
    contentLayout->addLayout(appInfoLayout);

    QLabel *deviceLabel = new QLabel("Choose Device:");
    {
        QFont f = deviceLabel->font();
        f.setPointSize(16);
        f.setBold(true);
        deviceLabel->setFont(f);
    }
    contentLayout->addWidget(deviceLabel);

    m_deviceCombo = new QComboBox();
    contentLayout->addWidget(m_deviceCombo);

    m_statusLabel = new QLabel("Ready to install");
    {
        QFont f = m_statusLabel->font();
        f.setPointSize(14);
        m_statusLabel->setFont(f);
    }
    m_statusLabel->setAlignment(Qt::AlignCenter);
    contentLayout->addWidget(m_statusLabel);

    contentLayout->addStretch();

    m_actionButton = new QPushButton("Install");
    m_actionButton->setFixedHeight(40);

    connect(m_actionButton, &QPushButton::clicked, this,
            &AppInstallDialog::onInstallClicked);
    contentLayout->addWidget(m_actionButton);

    m_cancelButton = new QPushButton("Cancel");
    m_cancelButton->setFixedHeight(40);
    connect(m_cancelButton, &QPushButton::clicked, this, &QDialog::reject);
    contentLayout->addWidget(m_cancelButton);

    m_loadingWidget->setupContentWidget(contentLayout);

    connect(AppContext::sharedInstance(), &AppContext::deviceChange, this,
            &AppInstallDialog::updateDeviceList);

    updateDeviceList();
}

void AppInstallDialog::updateDeviceList()
{
    m_deviceCombo->clear();
    auto devices = AppContext::sharedInstance()->getAllDevices();
    if (devices.empty()) {
        m_deviceCombo->addItem("No devices connected");
        m_deviceCombo->setEnabled(false);
        m_actionButton->setDefault(false);
        m_actionButton->setEnabled(false);
        m_statusLabel->setText("No devices connected");
        setLabelTextColor(m_statusLabel, Qt::red);
    } else {
        m_deviceCombo->setEnabled(true);
        for (const auto &device : devices) {
            QString deviceName =
                QString::fromStdString(device->deviceInfo.productType);
            m_deviceCombo->addItem(deviceName + " / " + device->udid.left(8) +
                                       "...",
                                   device->udid);
        }
        m_actionButton->setDefault(true);
        m_actionButton->setEnabled(true);
        m_statusLabel->setText("Ready to install");
        resetLabelTextColor(m_statusLabel);
    }
}

void AppInstallDialog::performInstallation(const QString &ipaPath,
                                           const QString &ipaName,
                                           const QString &deviceUdid)
{
    m_statusLabel->setText("Installing app...");
    // cannot cancel from this point
    m_cancelButton->setEnabled(false);
    resetLabelTextColor(m_statusLabel);

    std::shared_ptr<iDescriptorDevice> device =
        AppContext::sharedInstance()->getDevice(deviceUdid);

    QPointer<AppInstallDialog> safeThis = this;
    // install_ipa_init
    connect(device->service_manager, &CXX::ServiceManager::install_ipa_init,
            this, [safeThis, this](bool started, const QString &state) {
                if (!safeThis || !safeThis.data()) {
                    return;
                }

                if (!started) {
                    QString msg =
                        state.isEmpty()
                            ? QStringLiteral("Failed to start installation")
                            : state;
                    m_loadingWidget->showError(msg);
                    return;
                }

                if (!state.isEmpty()) {
                    m_statusLabel->setText(state);
                } else {
                    m_statusLabel->setText("Installing app...");
                }
                resetLabelTextColor(m_statusLabel);
            });

    // install_ipa_progress
    connect(device->service_manager, &CXX::ServiceManager::install_ipa_progress,
            this, [this, safeThis](double progress, const QString &state) {
                if (!safeThis || !safeThis.data()) {
                    return;
                }
                if (!state.isEmpty()) {
                    m_statusLabel->setText(state);
                }
                m_progressBar->setValue(static_cast<int>(progress * 100));

                // treat >= 100% as completion
                if (progress >= 1.0) {
                    m_cancelButton->setEnabled(true);
                    m_statusLabel->setText(
                        "Installation completed successfully!");
                    setLabelTextColor(m_statusLabel, Qt::darkGreen);
                    QMessageBox::information(this, "Success",
                                             "App installed successfully!");
                    accept();
                }
            });
    device->service_manager->install_ipa(ipaPath);
}

void AppInstallDialog::onInstallClicked()
{
    if (m_deviceCombo->count() == 0) {
        QMessageBox::warning(this, "No Device",
                             "Please connect a device first.");
        return;
    }

    m_deviceCombo->setEnabled(false);
    m_actionButton->setEnabled(false);
    m_statusLabel->setText("Downloading app...");
    resetLabelTextColor(m_statusLabel);

    QString selectedDevice = m_deviceCombo->currentData().toString();

    int buttonIndex = m_layout->indexOf(m_actionButton);
    layout()->removeWidget(m_actionButton);
    m_actionButton->deleteLater();
    m_actionButton = nullptr;

    if (m_tempDir) {
        delete m_tempDir;
        m_tempDir = nullptr;
    }
    // Create a new temporary directory for each installation
    m_tempDir = new QTemporaryDir();
    if (!m_tempDir->isValid()) {
        m_statusLabel->setText("Failed to create temporary directory");
        setLabelTextColor(m_statusLabel, Qt::red);
        QMessageBox::critical(
            this, "Error",
            "Could not create temporary directory for download.");
        return;
    }

    startDownloadProcess(m_bundleId, m_tempDir->path(), buttonIndex, false,
                         false);

    //  FIXME USE SAFETHIS
    connect(
        this, &AppDownloadBaseDialog::downloadFinished, this,
        [this, selectedDevice](bool success) {
            if (success) {
                qDebug() << "Download finished, starting installation...";
                /*
                    FIXME: libipatool generates random id and appends that
                   to the downloaded IPA filename, so we need to search for
                   it.
                */
                // Find the actual downloaded IPA file
                QDir outDir(m_tempDir->path());
                QStringList filters;
                filters << m_bundleId + "*.ipa";
                QStringList matches =
                    outDir.entryList(filters, QDir::Files, QDir::Time);
                if (matches.isEmpty()) {
                    m_statusLabel->setText("Download failed - IPA not found");
                    setLabelTextColor(m_statusLabel, Qt::red);
                    QMessageBox::critical(
                        this, "Error",
                        QString("Downloaded IPA not found in %1")
                            .arg(outDir.absolutePath()));
                    return;
                }
                qDebug() << "Found downloaded IPA:" << matches.first();
                QString ipaFile = outDir.filePath(matches.first());
                performInstallation(ipaFile, matches.first(), selectedDevice);

            } else {
                m_statusLabel->setText("Download failed");
                setLabelTextColor(m_statusLabel, Qt::red);
            }
        });
}

void AppInstallDialog::reject()
{
    // FIMXE: is this ok ?
    // if cancel button is already disabled, it means we're in the middle of
    // installation and cannot cancel
    if (!m_cancelButton->isEnabled()) {
        return;
    }

    if (m_statusLabel) {
        m_statusLabel->setText("Installation cancelled");
        setLabelTextColor(m_statusLabel, Qt::red);
    }

    AppDownloadBaseDialog::reject();
}

AppInstallDialog::~AppInstallDialog()
{
    if (m_tempDir) {
        delete m_tempDir;
        m_tempDir = nullptr;
    }
}