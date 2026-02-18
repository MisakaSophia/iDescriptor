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

#ifndef WIRELESSGALLERYIMPORTWIDGET_H
#define WIRELESSGALLERYIMPORTWIDGET_H

#include "iDescriptor-ui.h"
#include "qprocessindicator.h"
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QMediaPlayer>
#include <QPushButton>
#include <QScrollArea>
#include <QStringList>
#include <QVBoxLayout>
#include <QVideoWidget>
#include <QWidget>

class WirelessGalleryImportWidget : public Tool
{
    Q_OBJECT

public:
    explicit WirelessGalleryImportWidget(QWidget *parent = nullptr);

    QStringList getSelectedFiles() const;

private slots:
    void onBrowseFiles();
    void onImportPhotos();
    void onRemoveFile(int index);

private:
    QScrollArea *m_scrollArea;
    QWidget *m_scrollContent;
    QVBoxLayout *m_fileListLayout;
    QPushButton *m_browseButton;
    QPushButton *m_importButton;
    QLabel *m_statusLabel;

    QStringList m_selectedFiles;

    void setupUI();
    void updateFileList();
    void updateStatusLabel();
    bool isGalleryCompatible(const QString &filePath) const;
    QStringList getGalleryCompatibleExtensions() const;
};

#endif // WIRELESSGALLERYIMPORTWIDGET_H
