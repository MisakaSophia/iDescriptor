#ifndef ZLOADINGWIDGET_H
#define ZLOADINGWIDGET_H

#include <QStackedWidget>
#include <QWidget>

class ZLoadingWidget : public QStackedWidget
{
    Q_OBJECT
public:
    explicit ZLoadingWidget(bool start = true, QWidget *parent = nullptr);
    ~ZLoadingWidget();
    void stop(bool showContent = true);
    void showLoading();
    void setupContentWidget(QWidget *contentWidget);
    void setupContentWidget(QLayout *contentLayout);
    void setupErrorWidget(QWidget *errorWidget);
    void setupErrorWidget(const QString &errorMessage);
    void setupErrorWidget(QLayout *errorLayout);
    void setupAditionalWidget(QWidget *customWidget);
    void switchToWidget(QWidget *widget);
    void showError();

private:
    class QProcessIndicator *m_loadingIndicator = nullptr;
signals:
};

#endif // ZLOADINGWIDGET_H
