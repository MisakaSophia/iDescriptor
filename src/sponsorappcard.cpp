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

#include "sponsorappcard.h"
#include "appswidget.h"
#include "iDescriptor-ui.h"
#include "iDescriptor.h"
#include <QApplication>
#include <QDesktopServices>
#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QPainterPath>
#include <QStyle>
#include <QUrl>

SponsorAppCard::SponsorAppCard(QWidget *parent) : QWidget{parent}
{

    QHBoxLayout *layout = new QHBoxLayout(this);
    setMaximumHeight(200);
    setMaximumWidth(500);
    setObjectName("SponsorAppCard");
    setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    setStyleSheet("QWidget#SponsorAppCard { border: 1px solid #ddd; "
                  "border-radius: 8px; }");
    layout->setContentsMargins(15, 15, 15, 15);
    layout->setSpacing(15);

    // App icon
    QLabel *iconLabel = new QLabel();
    QPixmap placeholderIcon = QApplication::style()
                                  ->standardIcon(QStyle::SP_ComputerIcon)
                                  .pixmap(64, 64);
    iconLabel->setPixmap(placeholderIcon);
    iconLabel->setAlignment(Qt::AlignCenter);
    layout->addWidget(iconLabel);

    QString name = "Shopify";
    QString bundleId = "com.jadedpixel.shopify";
    QString description =
        "Create an online store within minutes and start selling.";
    QString websiteUrl = "https://www.shopify.com";

    ::fetchAppIconFromApple(
        m_networkManager, bundleId, [iconLabel](const QPixmap &pixmap, const QJsonObject &appInfo) {
            if (!pixmap.isNull()) {
                QPixmap scaled =
                    pixmap.scaled(64, 64, Qt::KeepAspectRatioByExpanding,
                                  Qt::SmoothTransformation);
                QPixmap rounded(64, 64);
                rounded.fill(Qt::transparent);

                QPainter painter(&rounded);
                painter.setRenderHint(QPainter::Antialiasing);
                QPainterPath path;
                path.addRoundedRect(QRectF(0, 0, 64, 64), 16, 16);
                painter.setClipPath(path);
                painter.drawPixmap(0, 0, scaled);
                painter.end();

                iconLabel->setPixmap(rounded);
            }
        });

    // Vertical layout for name and description
    QVBoxLayout *textLayout = new QVBoxLayout();

    // App name
    QLabel *nameLabel = new QLabel(name);
    nameLabel->setStyleSheet("font-size: 16px;");
    nameLabel->setAlignment(Qt::AlignCenter);
    nameLabel->setWordWrap(true);
    textLayout->addWidget(nameLabel);

    // App description
    QLabel *descLabel = new QLabel(description);
    descLabel->setStyleSheet("font-size: 12px; color: #666;");
    descLabel->setAlignment(Qt::AlignCenter);
    descLabel->setWordWrap(true);
    textLayout->addWidget(descLabel);

    layout->addLayout(textLayout);

    QVBoxLayout *buttonsLayout = new QVBoxLayout();

    // Install button placeholder
    ZLabel *installLabel = new ZLabel("Install");
    installLabel->setAlignment(Qt::AlignCenter);
    installLabel->setStyleSheet("font-size: 12px; color: #007AFF; font-weight: "
                                "bold; background-color: transparent;");
    installLabel->setCursor(Qt::PointingHandCursor);
    installLabel->setFixedHeight(30);

    ZLabel *websiteLabel = new ZLabel("Website");
    websiteLabel->setStyleSheet("font-size: 12px; font-weight: "
                                "bold; background-color: transparent;");
    websiteLabel->setAlignment(Qt::AlignCenter);
    websiteLabel->setCursor(Qt::PointingHandCursor);

    connect(installLabel, &ZLabel::clicked, this,
            [this, name, bundleId, description]() {
                AppsWidget::sharedInstance()->onAppCardClicked(name, bundleId,
                                                               description);
            });

    connect(websiteLabel, &ZLabel::clicked, this, [this, websiteUrl]() {
        QDesktopServices::openUrl(QUrl(websiteUrl));
    });

    buttonsLayout->addStretch();
    buttonsLayout->addWidget(installLabel);
    buttonsLayout->addWidget(websiteLabel);
    buttonsLayout->addStretch();

    layout->addLayout(buttonsLayout);
    // gridLayout->addWidget(cardWidget, row, col);
}
