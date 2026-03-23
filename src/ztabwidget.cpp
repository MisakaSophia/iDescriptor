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

#include "ztabwidget.h"
#include "iDescriptor-ui.h"
#include <QEasingCurve>
#include <QGraphicsDropShadowEffect>
#include <QMainWindow>
#include <QPainter>
#include <QPropertyAnimation>
#include <QSequentialAnimationGroup>
#include <QStyleOption>
#include <QTimer>

QRect gliderEndRectForTab(const ZTab *tab)
{
    if (!tab)
        return {};

    // Approximate "title center" using the push-button contents rect center.
    QStyleOptionButton opt;
    opt.initFrom(tab);
    opt.text = tab->text();
    opt.icon = tab->icon();
    opt.iconSize = tab->iconSize();

    QRect contents =
        tab->style()->subElementRect(QStyle::SE_PushButtonContents, &opt, tab);
    if (!contents.isValid())
        contents = tab->rect();

    const int centerX = tab->mapToParent(contents.center()).x();

    // Half-width glider, clamped so it never exceeds contents width and never
    // gets too tiny.
    const int rawW = tab->width() / 1.5;
    const int maxW = qMax(1, contents.width());
    const int w = qBound(12, qMin(rawW, maxW), tab->width());

    const int x = centerX - (w / 2);
    const int y = tab->pos().y() + tab->height() - 2;
    return QRect(x, y, w, 2);
}

ZTab::ZTab(const QString &text, QWidget *parent) : QPushButton(text, parent)
{
    setCheckable(true);
#ifndef WIN32
    setFixedHeight(40);
#else
    setFixedHeight(40);
#endif

    setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
}

ZTabWidget::ZTabWidget(QWidget *parent) : QWidget(parent), m_currentIndex(0)
{
    m_mainLayout = new QVBoxLayout(this);
    m_mainLayout->setContentsMargins(10, 0, 10, 0);
    m_mainLayout->setSpacing(0);

    // Create tab bar container
    m_tabBar = new QWidget();
#ifndef WIN32
    m_tabBar->setFixedHeight(40);
#else
    m_tabBar->setFixedHeight(40);
#endif
    m_tabBar->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    m_tabLayout = new QHBoxLayout(m_tabBar);
    m_tabLayout->setSpacing(0);
    m_tabLayout->setContentsMargins(0, 0, 0, 0);

    // Add drop shadow effect
    QGraphicsDropShadowEffect *shadow = new QGraphicsDropShadowEffect();
    shadow->setBlurRadius(20);
    shadow->setColor(QColor(24, 94, 224, 38)); // rgba(24, 94, 224, 0.15)
    shadow->setOffset(0, 6);
    m_tabBar->setGraphicsEffect(shadow);

    m_buttonGroup = new QButtonGroup(this);
    m_buttonGroup->setExclusive(true);

    // Create stacked widget for content
    m_stackedWidget = new QStackedWidget();
    m_stackedWidget->setSizePolicy(QSizePolicy::Expanding,
                                   QSizePolicy::Expanding);

    // Add widgets to layout
    m_mainLayout->addWidget(m_tabBar);
    m_mainLayout->addWidget(m_stackedWidget, 1);

    setupGlider();
}

void ZTabWidget::setupGlider()
{
    m_glider = new QWidget(m_tabBar);
    m_glider->setStyleSheet(QString("QWidget {"
                                    "  background-color: %1;"
                                    "  border-radius: %2px;"
                                    "}")
                                .arg(COLOR_ACCENT_BLUE.name())
#ifndef WIN32
                                .arg(6)
#else
                                .arg(2)
#endif
    );
    m_glider->hide(); // Hide initially until tabs are added
}

ZTab *ZTabWidget::addTab(QWidget *widget, const QString &label)
{
    ZTab *tab = new ZTab(label, m_tabBar);
    connect(tab, &ZTab::clicked, this, &ZTabWidget::onTabClicked);
    int index = m_tabs.count();
    m_tabs.append(tab);
    m_widgets.append(widget);

    m_tabLayout->addWidget(tab);
    m_stackedWidget->addWidget(widget);
    m_buttonGroup->addButton(tab, index);

    return tab;
}

void ZTabWidget::setCurrentIndex(int index)
{
    if (index < 0 || index >= m_tabs.count() || index == m_currentIndex) {
        return;
    }

    m_currentIndex = index;
    m_tabs[index]->setChecked(true);
    m_stackedWidget->setCurrentIndex(index);
#ifdef WIN32
    animateWidget(m_stackedWidget->currentWidget());
#endif
    updateTabStyles();
    animateGlider(index);

    emit currentChanged(index);
}

void ZTabWidget::finalizeStyles()
{
    if (m_tabs.isEmpty())
        return;

    m_currentIndex = 0;
    ZTab *tab = m_tabs[m_currentIndex];
    if (!tab)
        return;

    tab->setChecked(true);

    QTimer::singleShot(0, this, [this]() {
        if (m_currentIndex >= 0 && m_currentIndex < m_tabs.size()) {
#ifndef WIN32
            animateGlider(m_currentIndex, true);
            m_glider->show();
#else
            ZTab *tab = m_tabs[m_currentIndex];
            const QRect endRect = gliderEndRectForTab(tab);
            if (m_gliderAnimation) {
                m_gliderAnimation->stop();
                delete m_gliderAnimation;
                m_gliderAnimation = nullptr;
            }
            m_glider->setGeometry(endRect);
            m_glider->show();
#endif
        }
    });

    updateTabStyles();
}

int ZTabWidget::currentIndex() const { return m_currentIndex; }

QWidget *ZTabWidget::widget(int index) const
{
    if (index < 0 || index >= m_widgets.count()) {
        return nullptr;
    }
    return m_widgets[index];
}

void ZTabWidget::onTabClicked()
{
    ZTab *clickedTab = qobject_cast<ZTab *>(sender());
    if (!clickedTab)
        return;

    int index = m_tabs.indexOf(clickedTab);
    if (index != -1) {
        setCurrentIndex(index);
    }
}

void ZTabWidget::animateGlider(int index, bool onResize)
{
    if (index < 0 || index >= m_tabs.count())
        return;

    ZTab *targetTab = m_tabs[index];
    if (!targetTab)
        return;

    const QRect endRect = gliderEndRectForTab(targetTab);

#ifdef WIN32
    if (onResize || !m_glider->isVisible()) {
        if (m_gliderAnimation) {
            m_gliderAnimation->stop();
            delete m_gliderAnimation;
            m_gliderAnimation = nullptr;
        }
        m_glider->setGeometry(endRect);
        m_glider->show();
        return;
    }

    const QRect startRect = m_glider->geometry();

    const int left = qMin(startRect.left(), endRect.left());
    const int right = qMax(startRect.right(), endRect.right());
    const QRect stretchRect(left, endRect.y(), (right - left + 1), 2);

    if (m_gliderAnimation) {
        m_gliderAnimation->stop();
        delete m_gliderAnimation;
        m_gliderAnimation = nullptr;
    }

    auto *group = new QSequentialAnimationGroup(this);

    auto *expandAnim = new QPropertyAnimation(m_glider, "geometry");
    expandAnim->setDuration(130);
    expandAnim->setStartValue(startRect);
    expandAnim->setEndValue(stretchRect);
    expandAnim->setEasingCurve(QEasingCurve::OutCubic);

    auto *settleAnim = new QPropertyAnimation(m_glider, "geometry");
    settleAnim->setDuration(190);
    settleAnim->setStartValue(stretchRect);
    settleAnim->setEndValue(endRect);
    settleAnim->setEasingCurve(QEasingCurve::OutCubic);

    group->addAnimation(expandAnim);
    group->addAnimation(settleAnim);

    m_gliderAnimation = group;
    group->start();
#else
    if (m_gliderAnimation == nullptr) {
        m_gliderAnimation = new QPropertyAnimation(m_glider, "pos", this);
        static_cast<QPropertyAnimation *>(m_gliderAnimation)->setDuration(250);
        static_cast<QPropertyAnimation *>(m_gliderAnimation)
            ->setEasingCurve(QEasingCurve::OutCubic);
    }

    m_glider->setFixedSize(endRect.width(), 2);
    m_gliderAnimation->stop();
    static_cast<QPropertyAnimation *>(m_gliderAnimation)
        ->setStartValue(m_glider->pos());
    static_cast<QPropertyAnimation *>(m_gliderAnimation)
        ->setEndValue(endRect.topLeft());
    m_gliderAnimation->start();
#endif
}

void ZTabWidget::animateWidget(QWidget *widget)
{
#ifdef WIN32
    if (!widget)
        return;

    // FIXME: doesn't work on Tool tab because we are using opacity in
    // stylesheet
    QGraphicsOpacityEffect *opacityEffect =
        qobject_cast<QGraphicsOpacityEffect *>(widget->graphicsEffect());
    if (!opacityEffect) {
        opacityEffect = new QGraphicsOpacityEffect(widget);
        widget->setGraphicsEffect(opacityEffect);
    }

    QPropertyAnimation *opacityAnim =
        new QPropertyAnimation(opacityEffect, "opacity", this);
    opacityAnim->setDuration(350);
    opacityAnim->setStartValue(0.0);
    opacityAnim->setEndValue(1.0);
    opacityAnim->setEasingCurve(QEasingCurve::OutCubic);
    opacityAnim->start(QAbstractAnimation::DeleteWhenStopped);

    QPropertyAnimation *posAnim = new QPropertyAnimation(widget, "pos", this);
    posAnim->setDuration(350);
    posAnim->setStartValue(QPoint(widget->pos().x(), widget->pos().y() + 20));
    posAnim->setEndValue(widget->pos());
    posAnim->setEasingCurve(QEasingCurve::OutCubic);
    posAnim->start(QAbstractAnimation::DeleteWhenStopped);
#else
    Q_UNUSED(widget);
#endif
}

void ZTabWidget::updateTabStyles()
{
    const QString accentColor =

#ifdef WIN32
        COLOR_ACCENT_BLUE.name();
#else
        "#185ee0";
#endif

    for (int i = 0; i < m_tabs.count(); ++i) {
        ZTab *tab = m_tabs[i];
        if (tab->isChecked()) {
            tab->setStyleSheet(QString("ZTab {"
                                       "  color: %1;"
//    "  color: #d7e1f4ff;"
#ifdef WIN32
                                       "font-family : \"Segoe UI\", serif;"
#endif
                                       "  font-weight: 600;"
                                       "  font-size: 20px;"
                                       "  border: none;"
                                       "  outline: none;"
                                       "  background-color: transparent;"
                                       "}"
                                       "ZTab:hover {"
                                       "  background-color: transparent;"
                                       "}")
                                   .arg(accentColor));
        } else {
            tab->setStyleSheet(QString("ZTab {"
                                       "  color: #666;"
            //    "  color: #2b5693;"
#ifdef WIN32
                                       "font-family : \"Segoe UI\", serif;"
#endif
                                       "  font-weight: 600;"
                                       "  font-size: 20px;"
                                       "  border: none;"
                                       "  outline: none;"
                                       "  background-color: transparent;"
                                       "}"
                                       "ZTab:hover {"
                                       "  color: %1;"
                                       "  background-color: transparent;"
                                       "}")
                                   .arg(accentColor));
        }
    }
}

// Update glider position when widget is resized
void ZTabWidget::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    if (m_currentIndex >= 0 && m_currentIndex < m_tabs.count()) {
        animateGlider(m_currentIndex, true);
    }
}
