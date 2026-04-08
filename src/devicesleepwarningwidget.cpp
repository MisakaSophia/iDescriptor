#include "devicesleepwarningwidget.h"

DeviceSleepWarningWidget::DeviceSleepWarningWidget(QWidget *parent)
    : QDialog{parent}
{
#ifdef WIN32
    setupWinWindow(this);
#endif
    QVBoxLayout *mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(0);
    setMinimumWidth(400);

    m_loadingWidget = new ZLoadingWidget(false, this);
    mainLayout->addWidget(m_loadingWidget);

    QWidget *contentContainer = new QWidget(this);
    QVBoxLayout *contentLayout = new QVBoxLayout(contentContainer);
    contentLayout->setContentsMargins(0, 0, 0, 0);
    contentLayout->setSpacing(0);
    m_loadingWidget->setupContentWidget(contentContainer);

    m_mediaPlayer = new QMediaPlayer(this);
    m_videoWidget = new QVideoWidget(this);
    m_videoWidget->setMinimumSize(300, 400);
    m_videoWidget->setSizePolicy(QSizePolicy::Expanding,
                                 QSizePolicy::Expanding);
    m_videoWidget->setAspectRatioMode(Qt::KeepAspectRatio);
    m_mediaPlayer->setVideoOutput(m_videoWidget);

    connect(m_mediaPlayer,
            QOverload<QMediaPlayer::MediaStatus>::of(
                &QMediaPlayer::mediaStatusChanged),
            [this](QMediaPlayer::MediaStatus status) {
                if (status == QMediaPlayer::EndOfMedia) {
                    m_mediaPlayer->setPosition(0);
                    m_mediaPlayer->play();
                }
            });

    QLabel *title = new QLabel("Keep Your Device Awake", this);
    title->setAlignment(Qt::AlignCenter);
    title->setStyleSheet("QLabel { font-size: 18px; font-weight: bold; }");

    QLabel *description = new QLabel(
        R"(Please keep your device awake or unlocked while connected wirelessly to avoid disconnection)",
        this);
    description->setAlignment(Qt::AlignCenter);
    description->setWordWrap(true);

    QVBoxLayout *textLayout = new QVBoxLayout();
    textLayout->setContentsMargins(20, 20, 20, 20);
    textLayout->setSpacing(10);
    contentLayout->addWidget(title);
    textLayout->addWidget(description);

    contentLayout->addLayout(textLayout);
    contentLayout->addWidget(m_videoWidget);

    contentLayout->addSpacing(10);

    QCheckBox *dontShowAgain = new QCheckBox("Don't show this again", this);
    connect(dontShowAgain, &QCheckBox::toggled, this, [this](bool checked) {
        SettingsManager::sharedInstance()->setIsSleepyDeviceWarningDismissed(
            checked);
    });

    contentLayout->addWidget(dontShowAgain, 0, Qt::AlignCenter);
    contentLayout->addSpacing(10);
    m_mediaPlayer->setSource(QUrl("qrc:/resources/unlock.mp4"));
    QTimer::singleShot(500, this, &DeviceSleepWarningWidget::init);
}

void DeviceSleepWarningWidget::init()
{
    m_mediaPlayer->play();
    m_loadingWidget->stop();
}
