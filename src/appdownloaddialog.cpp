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

#include "appdownloaddialog.h"
#include "Backdrop.h"
#include "iDescriptor-ui.h"
#include "iDescriptor.h"
#include "libipatool-go.h"
#include <QDesktopServices>
#include <QFileDialog>
#include <QLabel>
#include <QNetworkAccessManager>
#include <QPointer>
#include <QPushButton>
#include <QStandardPaths>
#include <QVBoxLayout>

AppDownloadDialog::AppDownloadDialog(const QString &appName,
                                     const QString &bundleId,
                                     const QString &description,
                                     QWidget *parent)
    : AppDownloadBaseDialog(appName, bundleId, parent),
      m_outputDir(
          QStandardPaths::writableLocation(QStandardPaths::DownloadLocation)),
      m_bundleId(bundleId)
{
    setWindowTitle("Download " + appName + " IPA");
    setModal(true);
    setFixedWidth(500);

    m_loadingWidget = new ZLoadingWidget(this);
    layout()->addWidget(m_loadingWidget);
    QVBoxLayout *contentLayout = new QVBoxLayout(this);
    contentLayout->setContentsMargins(0, 0, 0, 0);

    m_bgLabel = new QLabel();
    // FIMXE: causes infinite scaling for some reason, need to investigate
    //  m_bgLabel->setScaledContents(true);
    //  m_bgLabel->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Ignored);
    //  contentLayout->addWidget(m_bgLabel);

    QPointer<AppDownloadDialog> safeThis = this;
    fetchAppIconFromApple(
        m_manager, bundleId,
        [safeThis](const QPixmap &icon, const QJsonObject &appInfo) {
            if (auto dialog = safeThis.data()) {
                if (!icon.isNull()) {
                    QPixmap blurred = BackDrop::blurPixmap(icon, 30);
                    dialog->m_bgLabel->setPixmap(blurred.scaled(
                        dialog->size(), Qt::KeepAspectRatioByExpanding,
                        Qt::SmoothTransformation));
                }
                dialog->m_loadingWidget->stop(true);
            }
        });

    QLabel *descLabel = new QLabel(description);
    descLabel->setWordWrap(true);
    descLabel->setStyleSheet("font-size: 14px; color: #666;");
    contentLayout->insertWidget(1, descLabel);

    // Directory selection UI
    QHBoxLayout *dirLayout = new QHBoxLayout();
    QLabel *dirTextLabel = new QLabel("Save to:");
    dirTextLabel->setStyleSheet("font-size: 14px;");
    dirLayout->addWidget(dirTextLabel);

    m_dirLabel = new ZLabel(this);
    m_dirLabel->setText(m_outputDir);
    m_dirLabel->setStyleSheet("font-size: 14px; color: #007AFF;");
    connect(m_dirLabel, &ZLabel::clicked, this, [this]() {
        QDesktopServices::openUrl(QUrl::fromLocalFile(m_outputDir));
    });
    m_dirLabel->setCursor(Qt::PointingHandCursor);
    dirLayout->addWidget(m_dirLabel, 1);

    m_dirButton = new QPushButton("Choose...");
    // m_dirButton->setStyleSheet("font-size: 14px; padding: 4px 12px;");
    connect(m_dirButton, &QPushButton::clicked, this, [this]() {
        QString dir = QFileDialog::getExistingDirectory(
            this, "Select Directory to Save IPA", m_outputDir);
        if (!dir.isEmpty()) {
            m_outputDir = dir;
            m_dirLabel->setText(m_outputDir);
        }
    });
    dirLayout->addWidget(m_dirButton);

    contentLayout->insertLayout(2, dirLayout);

    QHBoxLayout *buttonLayout = new QHBoxLayout();

    m_actionButton = new QPushButton("Download IPA");
    // m_actionButton->setFixedHeight(40);
    m_actionButton->setDefault(true);
    connect(m_actionButton, &QPushButton::clicked, this,
            &AppDownloadDialog::onDownloadClicked);
    buttonLayout->addWidget(m_actionButton);

    QPushButton *cancelButton = new QPushButton("Cancel");
    // cancelButton->setFixedHeight(40);
    // cancelButton->setStyleSheet(
    //     "background-color: #f0f0f0; color: #333; border: 1px solid #ddd; "
    //     "border-radius: 6px; font-size: 16px;");
    connect(cancelButton, &QPushButton::clicked, this, &QDialog::reject);
    buttonLayout->addWidget(cancelButton);

    contentLayout->addLayout(buttonLayout);

    m_loadingWidget->setupContentWidget(contentLayout);
}

void AppDownloadDialog::onDownloadClicked()
{
    // Disable directory selection once download starts
    m_dirButton->setEnabled(false);
    m_actionButton->setEnabled(false);

    int buttonIndex = m_layout->indexOf(m_actionButton);
    layout()->removeWidget(m_actionButton);
    m_actionButton->deleteLater();
    qDebug() << "Starting download to" << m_outputDir;
    qDebug() << "Bundle ID:" << m_bundleId;
    startDownloadProcess(m_bundleId, m_outputDir, buttonIndex);
}
