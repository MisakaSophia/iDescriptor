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

#ifndef QUERYMOBILEGESTALTWIDGET_H
#define QUERYMOBILEGESTALTWIDGET_H

#include "iDescriptor-ui.h"
#include "iDescriptor.h"
#include <QCheckBox>
#include <QComboBox>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QMap>
#include <QPushButton>
#include <QScrollArea>
#include <QStringList>
#include <QTextEdit>
#include <QVBoxLayout>
#include <QVariant>
#include <QWidget>

class QueryMobileGestaltWidget : public Tool
{
    Q_OBJECT

public:
    QueryMobileGestaltWidget(iDescriptorDevice *device,
                             QWidget *parent = nullptr);

private slots:
    void onQueryButtonClicked();
    void onSelectAllClicked();
    void onClearAllClicked();

private:
    void setupUI();
    void populateKeys();
    QStringList getSelectedKeys();
    void displayResults(const QMap<QString, QVariant> &results);

    // UI Components
    QVBoxLayout *mainLayout;
    QGroupBox *selectionGroup;
    QScrollArea *scrollArea;
    QWidget *checkboxWidget;
    QVBoxLayout *checkboxLayout;
    QHBoxLayout *buttonLayout;
    QPushButton *selectAllButton;
    QPushButton *clearAllButton;
    QPushButton *queryButton;
    QTextEdit *outputTextEdit;
    QLabel *statusLabel;
    iDescriptorDevice *m_device;

    // Data
    QStringList mobileGestaltKeys;
    QList<QCheckBox *> keyCheckboxes;

    // Mock query function for demonstration
    QMap<QString, QVariant> queryMobileGestalt(const QStringList &keys);
};

#endif // QUERYMOBILEGESTALTWIDGET_H