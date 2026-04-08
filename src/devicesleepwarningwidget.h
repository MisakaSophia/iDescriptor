#ifndef DEVICESLEEPWARNINGWIDGET_H
#define DEVICESLEEPWARNINGWIDGET_H

#include "iDescriptor-ui.h"
#include "iDescriptor.h"
#include "zloadingwidget.h"
#include <QCheckBox>
#include <QDialog>
#include <QMediaPlayer>
#include <QTimer>
#include <QVBoxLayout>
#include <QVideoWidget>
#include <QWidget>

class DeviceSleepWarningWidget : public QDialog
{
    Q_OBJECT
public:
    explicit DeviceSleepWarningWidget(QWidget *parent = nullptr);

private:
    ZLoadingWidget *m_loadingWidget;
    QMediaPlayer *m_mediaPlayer;
    QVideoWidget *m_videoWidget;

    void init();
};

#endif // DEVICESLEEPWARNINGWIDGET_H
