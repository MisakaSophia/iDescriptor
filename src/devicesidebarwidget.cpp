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

#include "devicesidebarwidget.h"
#include "appcontext.h"
#include "iDescriptor-ui.h"
#include "loadingspinnerwidget.h"
#include "qprocessindicator.h"
#include <QDebug>
#include <QMenu>

// DeviceSidebarItem Implementation
DeviceSidebarItem::DeviceSidebarItem(const QString &deviceName,
                                     const QString &uuid, bool isWireless,
                                     QWidget *parent)
    : QFrame(parent), m_deviceName(deviceName), m_uuid(uuid), m_selected(false),
      m_wireless(isWireless), m_collapsed(false)
{
    setupUI();
    setFrameStyle(QFrame::StyledPanel);
    setLineWidth(1);
    updateToggleButton();
    setObjectName("DeviceSidebarItem");
}

void DeviceSidebarItem::setupUI()
{
    m_mainLayout = new QVBoxLayout(this);
    m_mainLayout->setContentsMargins(5, 5, 5, 5);
    m_mainLayout->setSpacing(5);

    // Header section (always visible)
    m_headerWidget = new ClickableWidget();
    QVBoxLayout *headerLayout = new QVBoxLayout(m_headerWidget);
    headerLayout->setContentsMargins(0, 0, 0, 0);
    headerLayout->setSpacing(2);
    connect(m_headerWidget, &ClickableWidget::clicked, this,
            [this]() { emit deviceSelected(m_uuid); });

    // Device name label
    QHBoxLayout *nameLayout = new QHBoxLayout();
    nameLayout->setContentsMargins(0, 0, 0, 0);
    m_deviceLabel = new QLabel(m_deviceName);
#ifndef WIN32
    m_deviceLabel->setStyleSheet("QLabel { font-weight: bold;  }");
#endif
    m_deviceLabel->setWordWrap(true);
    nameLayout->addWidget(m_deviceLabel);
    if (m_wireless) {
        auto wirelessIcon = new ZIconLabel(
            QIcon(":/resources/icons/QlementineIconsWireless116.png"),
            "Wireless", 1.0, this);
        nameLayout->setSpacing(5);
        nameLayout->addWidget(wirelessIcon);
    }
    headerLayout->addLayout(nameLayout);

    // Toggle button
    m_toggleButton = new QPushButton();
    m_toggleButton->setFlat(true);
    m_toggleButton->setMaximumHeight(20);
    m_toggleButton->setStyleSheet("QPushButton { "
                                  "  text-align: left; "
                                  "  padding: 2px 5px; "
#ifdef WIN32
                                  " min-height: 0; "
#endif
                                  "  border: none; "
                                  "  color: #666; "
                                  "  font-size: 11px; "
                                  "} "
                                  "QPushButton:hover { "
                                  "  background-color: #f0f0f0; "
                                  "  border-radius: 3px; "
                                  "}");
    connect(m_toggleButton, &QPushButton::clicked, this,
            &DeviceSidebarItem::onToggleCollapse);
    headerLayout->addWidget(m_toggleButton);

    m_mainLayout->addWidget(m_headerWidget);

    // Options section (collapsible)
    m_optionsWidget = new QWidget();
    QVBoxLayout *optionsLayout = new QVBoxLayout(m_optionsWidget);
    optionsLayout->setContentsMargins(5, 5, 5, 5);
    optionsLayout->setSpacing(3);

    // Create navigation buttons
    m_infoButton = new QPushButton("Info");
    m_appsButton = new QPushButton("Apps");
    m_galleryButton = new QPushButton("Gallery");
    m_filesButton = new QPushButton("Files");

    // Create button group for exclusive selection
    m_navigationGroup = new QButtonGroup(this);
    m_navigationGroup->addButton(m_infoButton, 0);
    m_navigationGroup->addButton(m_appsButton, 1);
    m_navigationGroup->addButton(m_galleryButton, 2);
    m_navigationGroup->addButton(m_filesButton, 3);

    // Style the navigation buttons
    QList<QPushButton *> navButtons = {m_infoButton, m_appsButton,
                                       m_galleryButton, m_filesButton};
    for (QPushButton *btn : navButtons) {
        btn->setCheckable(true);
        btn->setMaximumHeight(25);
        btn->setStyleSheet(
            QString("QPushButton { "
                    "  background-color: rgba(255, 255, 255, 120); "
                    "  border: 1px solid rgba(255, 255, 255, 200); "
#ifdef WIN32
                    " min-height: 0; "
#endif
                    "  padding: 4px 8px; "
                    "  text-align: center; "
                    "  border-radius: 6px; "
                    "  font-size: 11px; "
                    "  color: #212529; "
                    "} "
                    "QPushButton:checked { "
                    "  background-color: %1; "
                    "  color: white; "
                    "  border: 1px solid %1; "
                    "} "
                    "QPushButton:hover:!checked { "
                    "  background-color: rgba(255, 255, 255, 180); "
                    "  border-color: %2; "
                    "} "
                    "QPushButton:checked:hover { "
                    "  background-color: %2; "
                    "}")
                .arg(COLOR_ACCENT_BLUE.name(), COLOR_BLUE.name()));

        connect(btn, &QPushButton::clicked, this,
                &DeviceSidebarItem::onNavigationButtonClicked);
        optionsLayout->addWidget(btn);
    }

    // Set default selection
    m_infoButton->setChecked(true);

    m_mainLayout->addWidget(m_optionsWidget);

    // Initialize UI state to match m_collapsed value
    // This ensures consistency regardless of initial m_collapsed value
    updateToggleButton();
    toggleCollapse();

    // Context menu: Remove
    setContextMenuPolicy(Qt::CustomContextMenu);
    connect(this, &QWidget::customContextMenuRequested, this,
            [this](const QPoint &pos) {
                QMenu menu(this);
                QAction *removeAct = menu.addAction("Remove");
                connect(removeAct, &QAction::triggered, this, [this]() {
                    AppContext::sharedInstance()->removeDevice(m_uuid, true);
                });
                menu.exec(mapToGlobal(pos));
            });

    setSelected(false);
}

void DeviceSidebarItem::setSelected(bool selected)
{
    m_selected = selected;
    bool dark = isDarkMode();

#ifndef WIN32
    if (selected) {
        setStyleSheet(QString("QFrame#DeviceSidebarItem { "
                              "background-color: rgba(255, 255, 255, 45); "
                              "border-radius: 5px; "
                              "border: 2px solid %1; "
                              "}")
                          .arg(COLOR_ACCENT_BLUE.name()));
    } else {
        setStyleSheet("QFrame#DeviceSidebarItem { "
                      "background-color: rgba(255, 255, 255, 16); "
                      "border-radius: 5px; "
                      "border: 2px solid transparent; "
                      "}");
    }
#else
    if (selected) {
        if (!dark) {
            setStyleSheet("QFrame#DeviceSidebarItem {     background-color: "
                          "rgba(0, 0, 0, 30); }");
        } else {
            setStyleSheet(
                "QFrame#DeviceSidebarItem {     background-color: rgba(255, "
                "255, 255, 45); }");
        }
    } else {
        if (!dark) {
            setStyleSheet("QFrame#DeviceSidebarItem {     background-color: "
                          "rgba(0, 0, 0, 10); }");
        } else {
            setStyleSheet(
                "QFrame#DeviceSidebarItem {     background-color: rgba(255, "
                "255, 255, 16); }");
        }
    }
#endif
}

void DeviceSidebarItem::setCollapsed(bool collapsed)
{
    if (m_collapsed == collapsed)
        return;

    m_collapsed = collapsed;
    updateToggleButton();
    toggleCollapse();
}

void DeviceSidebarItem::updateToggleButton()
{
    if (m_collapsed) {
        m_toggleButton->setText("▶ Options");
    } else {
        m_toggleButton->setText("▼ Options");
    }
}

void DeviceSidebarItem::toggleCollapse()
{
    if (m_collapsed) {
        m_optionsWidget->hide();
        m_optionsWidget->setMaximumHeight(0);
    } else {
        m_optionsWidget->show();
        m_optionsWidget->setMaximumHeight(QWIDGETSIZE_MAX);
    }
}

void DeviceSidebarItem::onToggleCollapse() { setCollapsed(!m_collapsed); }

void DeviceSidebarItem::onNavigationButtonClicked()
{
    QPushButton *button = qobject_cast<QPushButton *>(sender());
    if (button) {
        // Only emit navigationRequested - this will handle both device
        // selection and tab switching
        emit navigationRequested(m_uuid, button->text());
        // Remove this line: emit deviceSelected(m_uuid);
    }
}

const QString &DeviceSidebarItem::getDeviceUuid() const { return m_uuid; }

RecoveryDeviceSidebarItem::RecoveryDeviceSidebarItem(uint64_t ecid,
                                                     QWidget *parent)
    : QFrame(parent), m_ecid(ecid)
{
    setObjectName("RecoveryDeviceSidebarItem");
    setupUI();
    setSelected(false);
}

void RecoveryDeviceSidebarItem::setupUI()
{
    QVBoxLayout *mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(5, 5, 5, 5);
    mainLayout->setSpacing(5);

    ClickableWidget *headerWidget = new ClickableWidget();
    connect(headerWidget, &ClickableWidget::clicked, this,
            [this]() { emit recoveryDeviceSelected(m_ecid); });
    QVBoxLayout *headerLayout = new QVBoxLayout(headerWidget);
    headerLayout->setContentsMargins(0, 0, 0, 0);

    QLabel *titleLabel = new QLabel("Recovery Mode");
    titleLabel->setStyleSheet("QLabel { font-weight: bold; }");
    titleLabel->setWordWrap(true);
    headerLayout->addWidget(titleLabel);

    mainLayout->addWidget(headerWidget);

    // FIXME: ecid
    // Context menu: Remove
    // setContextMenuPolicy(Qt::CustomContextMenu);
    // connect(this, &QWidget::customContextMenuRequested, this,
    //         [this](const QPoint &pos) {
    //             QMenu menu(this);
    //             QAction *removeAct = menu.addAction("Remove");
    //             connect(removeAct, &QAction::triggered, this, [this]() {
    //                 AppContext::sharedInstance()->removeDevice(
    //                     QString::number(m_ecid));
    //             });
    //             menu.exec(mapToGlobal(pos));
    //         });
}

void RecoveryDeviceSidebarItem::setSelected(bool selected)
{
    m_selected = selected;
    bool dark = isDarkMode();

#ifndef WIN32
    if (selected) {
        setStyleSheet(QString("QFrame#RecoveryDeviceSidebarItem { "
                              "background-color: rgba(255, 255, 255, 45); "
                              "border-radius: 5px; "
                              "border: 2px solid %1; "
                              "}")
                          .arg(COLOR_ACCENT_BLUE.name()));
    } else {
        setStyleSheet("QFrame#RecoveryDeviceSidebarItem { "
                      "background-color: rgba(255, 255, 255, 16); "
                      "border-radius: 5px; "
                      "border: 2px solid transparent; "
                      "}");
    }
#else
    if (selected) {
        if (!dark) {
            setStyleSheet(
                "QFrame#RecoveryDeviceSidebarItem {     background-color: "
                "rgba(0, 0, 0, 30); }");
        } else {
            setStyleSheet("QFrame#RecoveryDeviceSidebarItem {     "
                          "background-color: rgba(255, "
                          "255, 255, 45); }");
        }
    } else {
        if (!dark) {
            setStyleSheet(
                "QFrame#RecoveryDeviceSidebarItem {     background-color: "
                "rgba(0, 0, 0, 10); }");
        } else {
            setStyleSheet("QFrame#RecoveryDeviceSidebarItem {     "
                          "background-color: rgba(255, "
                          "255, 255, 16); }");
        }
    }
#endif
}

// DeviceSidebarWidget Implementation
DeviceSidebarWidget::DeviceSidebarWidget(QWidget *parent)
    : QWidget(parent), m_currentSelection(DeviceSelection(""))
{
    QVBoxLayout *mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(10, 10, 10, 10);
    mainLayout->setSpacing(0);

    m_scrollArea = new QScrollArea();
    m_scrollArea->setWidgetResizable(true);
    m_scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_scrollArea->setFrameStyle(QFrame::NoFrame);

    // Make the scroll area and its viewport transparent
    m_scrollArea->setStyleSheet(
        "QScrollArea { background: transparent; border: none; }");
    m_scrollArea->viewport()->setStyleSheet("background: transparent;");

    m_contentWidget = new QWidget();
    m_contentLayout = new QVBoxLayout(m_contentWidget);
    m_contentLayout->setContentsMargins(5, 5, 5, 5);
    m_contentLayout->setSpacing(10);
    m_contentLayout->addStretch(); // Push items to top

    m_contentWidget->setStyleSheet("background: transparent;");

    m_scrollArea->setWidget(m_contentWidget);
    mainLayout->addWidget(m_scrollArea);

    setMinimumWidth(200);
    setMaximumWidth(200);

    // Listen to AppContext selection changes
    connect(AppContext::sharedInstance(),
            &AppContext::currentDeviceSelectionChanged, this,
            &DeviceSidebarWidget::setCurrentSelection);
}

DeviceSidebarItem *DeviceSidebarWidget::addDevice(const QString &deviceName,
                                                  const QString &uuid,
                                                  bool isWireless)
{
    DeviceSidebarItem *item =
        new DeviceSidebarItem(deviceName, uuid, isWireless, this);

    // Connect to unified handler
    connect(
        item, &DeviceSidebarItem::deviceSelected, this,
        [this](const QString &uuid) { onItemSelected(DeviceSelection(uuid)); });
    connect(item, &DeviceSidebarItem::navigationRequested, this,
            [this](const QString &uuid, const QString &section) {
                onItemSelected(DeviceSelection(uuid, section));
            });

    m_deviceItems[uuid] = item;
    m_contentLayout->insertWidget(m_contentLayout->count() - 1, item);

    return item;
}

DevicePendingSidebarItem *
DeviceSidebarWidget::addPendingDevice(const QString &uuid)
{
    DevicePendingSidebarItem *item = new DevicePendingSidebarItem(uuid, this);
    connect(item, &DevicePendingSidebarItem::clicked, this,
            [this, uuid]() { onItemSelected(DeviceSelection::pending(uuid)); });
    m_pendingItems[uuid] = item;
    m_contentLayout->insertWidget(m_contentLayout->count() - 1, item);
    return item;
}

RecoveryDeviceSidebarItem *DeviceSidebarWidget::addRecoveryDevice(uint64_t ecid)
{
    RecoveryDeviceSidebarItem *item = new RecoveryDeviceSidebarItem(ecid, this);

    connect(item, &RecoveryDeviceSidebarItem::recoveryDeviceSelected, this,
            [this](uint64_t ecid) { onItemSelected(DeviceSelection(ecid)); });

    m_recoveryItems[ecid] = item;
    m_contentLayout->insertWidget(m_contentLayout->count() - 1, item);
    return item;
}

void DeviceSidebarWidget::removeDevice(const QString &uuid)
{
    if (m_deviceItems.contains(uuid)) {
        DeviceSidebarItem *item = m_deviceItems[uuid];
        m_deviceItems.remove(uuid);
        m_contentLayout->removeWidget(item);
        item->deleteLater();
    }
}

void DeviceSidebarWidget::removePendingDevice(const QString &uuid)
{
    if (m_pendingItems.contains(uuid)) {
        DevicePendingSidebarItem *item = m_pendingItems[uuid];
        m_pendingItems.remove(uuid);
        m_contentLayout->removeWidget(item);
        item->deleteLater();
    }
}

void DeviceSidebarWidget::removeRecoveryDevice(uint64_t ecid)
{
    if (m_recoveryItems.contains(ecid)) {
        RecoveryDeviceSidebarItem *item = m_recoveryItems[ecid];
        m_recoveryItems.remove(ecid);
        m_contentLayout->removeWidget(item);
        item->deleteLater();
    }
}

void DeviceSidebarWidget::setCurrentSelection(const DeviceSelection &selection)
{
    m_currentSelection = selection;
    updateSelection();
}

void DeviceSidebarWidget::onItemSelected(const DeviceSelection &selection)
{
    AppContext::sharedInstance()->setCurrentDeviceSelection(selection);
}

void DeviceSidebarWidget::updateSelection()
{
    // Clear all selections first
    for (auto item : m_deviceItems) {
        item->setSelected(false);
    }
    for (auto item : m_recoveryItems) {
        item->setSelected(false);
    }
    for (auto item : m_pendingItems) {
        item->setSelected(false);
    }

    // Set selection based on current selection
    if (m_currentSelection.type == DeviceSelection::Normal &&
        m_deviceItems.contains(m_currentSelection.udid)) {
        m_deviceItems[m_currentSelection.udid]->setSelected(true);
    } else if (m_currentSelection.type == DeviceSelection::Recovery &&
               m_recoveryItems.contains(m_currentSelection.ecid)) {
        m_recoveryItems[m_currentSelection.ecid]->setSelected(true);
    } else if (m_currentSelection.type == DeviceSelection::Pending &&
               m_pendingItems.contains(m_currentSelection.udid)) {
        m_pendingItems[m_currentSelection.udid]->setSelected(true);
    }
}

DevicePendingSidebarItem::DevicePendingSidebarItem(const QString &udid,
                                                   QWidget *parent)
    : QFrame(parent), m_udid(udid)
{
    QHBoxLayout *layout = new QHBoxLayout(this);
    layout->setContentsMargins(10, 10, 10, 10);
    layout->setSpacing(1);

    QProcessIndicator *spinner = new QProcessIndicator(this);
    spinner->setFixedSize(26, 26);
    spinner->setType(QProcessIndicator::line_rotate);
    spinner->start();

    QLabel *label = new QLabel("Pairing...", this);

    layout->addWidget(label);
    layout->addWidget(spinner);

    setLayout(layout);
    setObjectName("DevicePendingSidebarItem");
    setSelected(false);
}

void DevicePendingSidebarItem::setSelected(bool selected)
{
    m_selected = selected;
    bool dark = isDarkMode();

#ifndef WIN32
    if (selected) {
        setStyleSheet(QString("QFrame#DevicePendingSidebarItem { "
                              "background-color: rgba(255, 255, 255, 45); "
                              "border-radius: 5px; "
                              "border: 2px solid %1; "
                              "}")
                          .arg(COLOR_ACCENT_BLUE.name()));
    } else {
        setStyleSheet("QFrame#DevicePendingSidebarItem { "
                      "background-color: rgba(255, 255, 255, 16); "
                      "border-radius: 5px; "
                      "border: 2px solid transparent; "
                      "}");
    }
#else
    if (selected) {
        if (!dark) {
            setStyleSheet(
                "QFrame#DevicePendingSidebarItem {     background-color: "
                "rgba(0, 0, 0, 30); }");
        } else {
            setStyleSheet("QFrame#DevicePendingSidebarItem {     "
                          "background-color: rgba(255, "
                          "255, 255, 45); }");
        }
    } else {
        if (!dark) {
            setStyleSheet(
                "QFrame#DevicePendingSidebarItem {     background-color: "
                "rgba(0, 0, 0, 10); }");
        } else {
            setStyleSheet("QFrame#DevicePendingSidebarItem {     "
                          "background-color: rgba(255, "
                          "255, 255, 16); }");
        }
    }
#endif
}

void DevicePendingSidebarItem::mousePressEvent(QMouseEvent *event)
{
    emit clicked();
    QFrame::mousePressEvent(event);
}