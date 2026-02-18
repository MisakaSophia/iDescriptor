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

#ifndef CABLEINFOWIDGET_H
#define CABLEINFOWIDGET_H

#include "iDescriptor-ui.h"
#include "iDescriptor.h"
#include "zloadingwidget.h"
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QProgressBar>
#include <QPushButton>
#include <QVBoxLayout>
#include <QWidget>

class CableInfoWidget : public Tool
{
    Q_OBJECT

public:
    explicit CableInfoWidget(iDescriptorDevice *device,
                             QWidget *parent = nullptr);

private slots:
    void initCableInfo();

private:
    void setupUI();
    void analyzeCableInfo();
    void updateUI();
    void createInfoRow(QGridLayout *layout, int row, const QString &label,
                       const QString &value);

    // Cable information structure
    struct CableInfo {
        bool isConnected = false;
        bool isGenuine = false;
        bool isTypeC = false;
        QString manufacturer;
        QString modelNumber;
        QString accessoryName;
        QString serialNumber;
        QString interfaceModuleSerial;
        uint64_t currentLimit = 0;
        uint64_t chargingVoltage = 0;
        QString connectionType;
        QString triStarClass;
        QStringList supportedTransports;
        QStringList activeTransports;
        bool isFakeInfo = false;
    };

    // UI components
    QVBoxLayout *m_mainLayout;
    QLabel *m_statusLabel;
    QLabel *m_descriptionLabel;
    QGroupBox *m_infoWidget;
    QGridLayout *m_infoLayout;
    ZLoadingWidget *m_loadingWidget;
    QLabel *m_errorLabel;

    // Data
    iDescriptorDevice *m_device;
    CableInfo m_cableInfo;
    plist_t m_response;
};

#endif // CABLEINFOWIDGET_H