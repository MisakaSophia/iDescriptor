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

#ifndef APPDOWNLOADDIALOG_H
#define APPDOWNLOADDIALOG_H

#include "appdownloadbasedialog.h"
#include "iDescriptor-ui.h"
#include "zloadingwidget.h"
#include <QDialog>
#include <QLabel>
#include <QPushButton>

class AppDownloadDialog : public AppDownloadBaseDialog
{
    Q_OBJECT
public:
    explicit AppDownloadDialog(const QString &appName, const QString &bundleId,
                               const QString &description,
                               QWidget *parent = nullptr);

protected:
    void resizeEvent(QResizeEvent *event) override
    {
        QWidget::resizeEvent(event);
        m_bgLabel->setFixedSize(event->size());
    }

private slots:
    void onDownloadClicked();

private:
    QString m_outputDir;
    QPushButton *m_dirButton;
    ZLabel *m_dirLabel;
    QString m_bundleId;
    QLabel *m_bgLabel;
    ZLoadingWidget *m_loadingWidget;
};

#endif // APPDOWNLOADDIALOG_H
