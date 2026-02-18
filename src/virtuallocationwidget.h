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

#ifndef VIRTUAL_LOCATION_H
#define VIRTUAL_LOCATION_H

#include "devdiskimagehelper.h"
#include "iDescriptor-ui.h"
#include "iDescriptor.h"
#include <QGroupBox>
#include <QLineEdit>
#include <QPushButton>
#include <QQuickWidget>
#include <QTimer>
#include <QVBoxLayout>
#include <QWidget>

class VirtualLocation : public Tool
{
    Q_OBJECT

public:
    explicit VirtualLocation(iDescriptorDevice *device,
                             QWidget *parent = nullptr);

signals:
    void locationChanged(double latitude, double longitude);

public slots:
    void updateInputsFromMap(double latitude, double longitude);

private slots:
    void onQuickWidgetStatusChanged(QQuickWidget::Status status);
    void onInputChanged();
    void onMapCenterChanged();
    void onApplyClicked();
    void updateMapFromInputs();
    void onRecentLocationClicked(double latitude, double longitude);

private:
    void loadRecentLocations(QVBoxLayout *layout);
    void refreshRecentLocations();
    void addLocationButtons(QLayout *layout,
                            QList<QVariantMap> recentLocations);

    QQuickWidget *m_quickWidget;
    QLineEdit *m_latitudeEdit;
    QLineEdit *m_longitudeEdit;
    QPushButton *m_applyButton;
    QTimer m_updateTimer;
    bool m_updatingFromInput = false;
    iDescriptorDevice *m_device;
    QVBoxLayout *m_rightLayout = nullptr;
    QGroupBox *m_recentGroup = nullptr;
};

#endif // VIRTUAL_LOCATION_H