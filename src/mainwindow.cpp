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

#include "mainwindow.h"
#include "./ui_mainwindow.h"
#include "appswidget.h"
#include "devicemanagerwidget.h"
#include "iDescriptor-ui.h"
#include "iDescriptor.h"
#include "ifusediskunmountbutton.h"
#include "ifusemanager.h"
#include "jailbrokenwidget.h"
// #ifdef ENABLE_RECOVERY_DEVICE_SUPPORT
// #include "libirecovery.h"
// #endif
#include "toolboxwidget.h"
#include "welcomewidget.h"
#include <QHBoxLayout>
#include <QStack>
#include <QStackedWidget>
#include <QTimer>
#include <QVBoxLayout>
#include <QWidget>
#include <unistd.h>

#include "appcontext.h"
#include "settingsmanager.h"
// #include "devicemonitor.h"
// #include "Toast.h"
#include "networkdevicemanager.h"
#include "networkdeviceswidget.h"
#include "statusballoon.h"
#include <QApplication>
#include <QDesktopServices>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>

#ifdef WIN32
#include "platform/windows/check_deps.h"
#endif

using namespace IdeviceFFI;

// void handleCallback(const idevice_event_t *event, void *userData)
// {
//     printf("Device event received: ");

//     switch (event->event) {
//     case IDEVICE_DEVICE_ADD: {
//         /* this should never happen iDescriptor does not support network
//         devices but for some reason even though we are only listening for USB
//         devices, we still get network devices on macOS*/ if (event->conn_type
//         == CONNECTION_NETWORK) {
//             return;
//         }
//         qDebug() << "Device added: " << QString::fromUtf8(event->udid);

//         QMetaObject::invokeMethod(
//             AppContext::sharedInstance(), "addDevice", Qt::QueuedConnection,
//             Q_ARG(QString, QString::fromUtf8(event->udid)),
//             Q_ARG(idevice_connection_type, event->conn_type),
//             Q_ARG(AddType, AddType::Regular));
//         break;
//     }

//     case IDEVICE_DEVICE_REMOVE: {
//         QMetaObject::invokeMethod(AppContext::sharedInstance(),
//         "removeDevice",
//                                   Qt::QueuedConnection,
//                                   Q_ARG(QString, QString(event->udid)));
//         break;
//     }

//     case IDEVICE_DEVICE_PAIRED: {
//         if (event->conn_type == CONNECTION_NETWORK) {
//             qDebug()
//                 << "Network devices are not supported but a network device
//                 was "
//                    "received in event listener. Please report this issue.";
//             return;
//         }
//         qDebug() << "Device paired: " << QString::fromUtf8(event->udid);

//         QMetaObject::invokeMethod(
//             AppContext::sharedInstance(), "addDevice", Qt::QueuedConnection,
//             Q_ARG(QString, QString::fromUtf8(event->udid)),
//             Q_ARG(idevice_connection_type, event->conn_type),
//             Q_ARG(AddType, AddType::Pairing));
//         break;
//     }
//     default:
//         qDebug() << "Unhandled event: " << event->event;
//     }
// }

// #ifdef ENABLE_RECOVERY_DEVICE_SUPPORT
// void handleCallbackRecovery(const irecv_device_event_t *event, void
// *userData)
// {

//     switch (event->type) {
//     case IRECV_DEVICE_ADD:
//         qDebug() << "Recovery device added: ";
//         QMetaObject::invokeMethod(AppContext::sharedInstance(),
//                                   "addRecoveryDevice", Qt::QueuedConnection,
//                                   Q_ARG(uint64_t, event->device_info->ecid));
//         break;
//     case IRECV_DEVICE_REMOVE:
//         qDebug() << "Recovery device removed: ";
//         QMetaObject::invokeMethod(AppContext::sharedInstance(),
//                                   "removeRecoveryDevice",
//                                   Qt::QueuedConnection, Q_ARG(uint64_t,
//                                   event->device_info->ecid));
//         break;
//     default:
//         printf("Unhandled recovery event: %d\n", event->type);
//     }
// }
// irecv_device_event_context_t context;
// #endif

MainWindow *MainWindow::sharedInstance()
{
    static MainWindow instance;
    return &instance;
}

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent), ui(new Ui::MainWindow)
{
    ui->setupUi(this);
    setMinimumSize(MIN_MAIN_WINDOW_SIZE);
    resize(MIN_MAIN_WINDOW_SIZE);
    m_ZTabWidget = new ZTabWidget(this);
    m_ZTabWidget->setAttribute(Qt::WA_ContentsMarginsRespectsSafeArea, false);

    setContentsMargins(0, 0, 0, 0);
#ifdef __APPLE__
    setupMacOSWindow(this);
    setAttribute(Qt::WA_ContentsMarginsRespectsSafeArea, false);
#endif
    setCentralWidget(m_ZTabWidget);

    m_mainStackedWidget = new QStackedWidget();
    WelcomeWidget *welcomePage = new WelcomeWidget(this);
    m_deviceManager = new DeviceManagerWidget(this);

    m_mainStackedWidget->addWidget(welcomePage);
    m_mainStackedWidget->addWidget(m_deviceManager);

    connect(m_deviceManager, &DeviceManagerWidget::updateNoDevicesConnected,
            this, &MainWindow::updateNoDevicesConnected);

    m_ZTabWidget->addTab(m_mainStackedWidget, "iDevice");
    auto *appsWidgetTab =
        m_ZTabWidget->addTab(AppsWidget::sharedInstance(), "Apps");
    m_ZTabWidget->addTab(new ToolboxWidget(this), "Toolbox");
    auto *jailbrokenWidget = new JailbrokenWidget(this);
    m_ZTabWidget->addTab(jailbrokenWidget, "Jailbroken");
    m_ZTabWidget->finalizeStyles();

    connect(
        appsWidgetTab, &ZTab::clicked, this,
        [this](int index) { AppsWidget::sharedInstance()->init(); },
        Qt::SingleShotConnection);

    // settings button
    ZIconWidget *settingsButton = new ZIconWidget(
        QIcon(":/resources/icons/MingcuteSettings7Line.png"), "Settings");
    settingsButton->setCursor(Qt::PointingHandCursor);
    connect(settingsButton, &ZIconWidget::clicked, this, [this]() {
        SettingsManager::sharedInstance()->showSettingsDialog();
    });

    ZIconWidget *githubButton = new ZIconWidget(
        QIcon(":/resources/icons/MdiGithub.png"), "iDescriptor on GitHub");
    githubButton->setCursor(Qt::PointingHandCursor);
    connect(githubButton, &ZIconWidget::clicked, this,
            []() { QDesktopServices::openUrl(QUrl(REPO_URL)); });

    m_connectedDeviceCountLabel = new QLabel("iDescriptor: no devices");
    m_connectedDeviceCountLabel->setContentsMargins(5, 0, 5, 0);
    m_connectedDeviceCountLabel->setStyleSheet(
        "QLabel:hover { background-color : #13131319; }");

    ui->statusbar->addWidget(m_connectedDeviceCountLabel);
    // TODO: implement downloads/uploads progress stuff

    StatusBalloon *statusBalloon = StatusBalloon::sharedInstance();

    ui->statusbar->addWidget(statusBalloon->getButton());

    ui->statusbar->setContentsMargins(0, 0, 0, 0);
    QLabel *appVersionLabel = new QLabel(QString("v%1").arg(APP_VERSION));
    appVersionLabel->setContentsMargins(5, 0, 5, 0);
    appVersionLabel->setStyleSheet(
        "QLabel:hover { background-color : #13131319; }");
    ui->statusbar->addPermanentWidget(appVersionLabel);
    ui->statusbar->addPermanentWidget(githubButton);
    ui->statusbar->addPermanentWidget(settingsButton);
#ifdef WIN32
    ui->statusbar->setStyleSheet(
        "QStatusBar { border-top: 1px solid #dcdcdc; }");
#endif

#ifdef __linux__
    QList<QString> mounted_iFusePaths = iFuseManager::getMountPoints();

    for (const QString &path : mounted_iFusePaths) {
        auto *p = new iFuseDiskUnmountButton(path);

        ui->statusbar->addPermanentWidget(p);
        connect(p, &iFuseDiskUnmountButton::clicked, this, [this, p, path]() {
            bool ok = iFuseManager::linuxUnmount(path);
            if (!ok) {
                QMessageBox::warning(nullptr, "Unmount Failed",
                                     "Failed to unmount iFuse at " + path +
                                         ". Please try again.");
                return;
            }
            ui->statusbar->removeWidget(p);
            p->deleteLater();
        });
    }
#endif

    // #ifdef ENABLE_RECOVERY_DEVICE_SUPPORT
    //     irecv_error_t res_recovery =
    //         irecv_device_event_subscribe(&context, handleCallbackRecovery,
    //         nullptr);

    //     if (res_recovery != IRECV_E_SUCCESS) {
    //         qDebug() << "ERROR: Unable to subscribe to recovery device
    //         events. "
    //                     "Error code:"
    //                  << res_recovery;
    //     }
    //     qDebug() << "Subscribed to recovery device events successfully.";
    // #endif

    //     idevice_error_t res = idevice_event_subscribe(handleCallback,
    //     nullptr); if (res != IDEVICE_E_SUCCESS) {
    //         qDebug() << "ERROR: Unable to subscribe to device events. Error
    //         code:"
    //                  << res;
    //     }
    //     qDebug() << "Subscribed to device events successfully.";
    //     createMenus();

    //     UpdateProcedure updateProcedure;
    //     bool packageManagerManaged = false;
    //     bool isPortable = false;
    //     bool skipPrerelease = true;
    // #ifdef WIN32
    //     isPortable = !is_iDescriptorInstalled();
    //     qDebug() << "isPortable=" << isPortable;
    // #endif

    /*
    struct UpdateProcedure {
        bool openFile;
        bool openFileDir;
        bool quitApp;
        QString boxInformativeText;
        QString boxText;
    };
    */
    //     switch (ZUpdater::detectPlatform()) {
    //     case Platform::Windows:
    //         updateProcedure = UpdateProcedure{
    //             !isPortable,
    //             isPortable,
    //             !isPortable,
    //             isPortable ? "New portable version downloaded, app location
    //             will "
    //                          "be shown after this message"
    //                        : "The application will now quit to install the
    //                        update.",
    //             isPortable ? "New portable version downloaded"
    //                        : "Do you want to install the downloaded update
    //                        now?",
    //         };
    //         break;
    //         // todo: adjust for pkg managers
    //     case Platform::MacOS:
    //         updateProcedure = UpdateProcedure{
    //             true,
    //             false,
    //             true,
    //             "The application will now quit and open .dmg file downloaded
    //             to "
    //             "\"Downloads\" from there you can drag it to Applications to
    //             " "install.", "Update downloaded would you like to quit and
    //             install the update?",
    //         };
    //         break;
    //     case Platform::Linux:
    //         // currently only on linux (arch aur) is enabled
    // #ifdef PACKAGE_MANAGER_MANAGED
    //         packageManagerManaged = true;
    // #endif
    //         updateProcedure = UpdateProcedure{
    //             true,
    //             false,
    //             true,
    //             "AppImages we ship are not updateable. New version is
    //             downloaded " "to "
    //             "\"Downloads\". You can start using the new version by
    //             launching " "it " "from there. You can delete this AppImage
    //             version if you like.", "Update downloaded would you like to
    //             quit and open the new " "version?",
    //         };
    //         break;
    //     default:
    //         updateProcedure = UpdateProcedure{
    //             false, false, false, "", "",
    //         };
    //     }

    //     m_updater = new ZUpdater("iDescriptor/iDescriptor", APP_VERSION,
    //                              "iDescriptor", updateProcedure, isPortable,
    //                              packageManagerManaged, skipPrerelease,
    //                              this);
    // #if defined(PACKAGE_MANAGER_MANAGED) && defined(__linux__)
    //     m_updater->setPackageManagerManagedMessage(
    //         QString(
    //             "You seem to have installed iDescriptor using a package
    //             manager. " "Please use %1 to update it.")
    //             .arg(PACKAGE_MANAGER_HINT));
    // #endif

    //     SettingsManager::sharedInstance()->doIfEnabled(
    //         SettingsManager::Setting::AutoCheckUpdates, [this]() {
    //             qDebug() << "Checking for updates...";
    //             m_updater->checkForUpdates();
    //         });

    // Usage in main thread:
    m_deviceMonitor = new DeviceMonitorThread(this);
    connect(
        m_deviceMonitor, &DeviceMonitorThread::deviceEvent, this,
        [this](int event, const QString &udid, int conn_type, int addType) {
            // Handle device connection
            switch (event) {
            case DeviceMonitorThread::IDEVICE_DEVICE_ADD: {
                /* never gets fired on any platform */
                if (conn_type == DeviceMonitorThread::CONNECTION_NETWORK) {
                    return;
                }
                qDebug() << "Device added: " << udid;

                QMetaObject::invokeMethod(
                    AppContext::sharedInstance(), "addDevice",
                    Qt::QueuedConnection, Q_ARG(QString, udid),
                    Q_ARG(
                        DeviceMonitorThread::IdeviceConnectionType,
                        static_cast<DeviceMonitorThread::IdeviceConnectionType>(
                            conn_type)),
                    Q_ARG(AddType, AddType::Regular));
                break;
            }

            case DeviceMonitorThread::IDEVICE_DEVICE_REMOVE: {
                QMetaObject::invokeMethod(AppContext::sharedInstance(),
                                          "removeDevice", Qt::QueuedConnection,
                                          Q_ARG(QString, udid));
                break;
            }

            case DeviceMonitorThread::IDEVICE_DEVICE_PAIRED: {
                /* never gets fired on any platform */
                if (conn_type == DeviceMonitorThread::CONNECTION_NETWORK) {
                    return;
                }
                qDebug() << "Device paired: " << udid;

                QMetaObject::invokeMethod(
                    AppContext::sharedInstance(), "addDevice",
                    Qt::QueuedConnection, Q_ARG(QString, udid),
                    Q_ARG(
                        DeviceMonitorThread::IdeviceConnectionType,
                        static_cast<DeviceMonitorThread::IdeviceConnectionType>(
                            conn_type)),
                    Q_ARG(AddType, AddType::Pairing));
                break;
            }
            default:
                qDebug() << "Unhandled event: " << event;
            }
        });

    /* If a device is connected before starting the app on slower machines ui
     * takes a lot of time to render so delay the monitoring a bit  */
    QTimer::singleShot(std::chrono::seconds(1), this,
                       [this]() { m_deviceMonitor->start(); });

    // ═══════════════════════════════════════════════════════════════════════
    //  Upgrade to wireless when a "WIRED" device is removed
    // ═══════════════════════════════════════════════════════════════════════
    connect(AppContext::sharedInstance(), &AppContext::deviceRemoved, this,
            [](const std::string &udid, const std::string &wifiMacAddress,
               const std::string &ipAddress, bool wasWireless) {
                if (wasWireless)
                    return;
                qDebug() << "Upgrading device to wireless connection for UDID"
                         << QString::fromStdString(udid);
                QMetaObject::invokeMethod(
                    AppContext::sharedInstance(), "addDevice",
                    Qt::QueuedConnection,
                    Q_ARG(QString, QString::fromStdString(udid)),
                    Q_ARG(DeviceMonitorThread::IdeviceConnectionType,
                          DeviceMonitorThread::CONNECTION_NETWORK),
                    Q_ARG(AddType, AddType::UpgradeToWireless),
                    Q_ARG(QString, QString::fromStdString(wifiMacAddress)),
                    Q_ARG(QString, QString::fromStdString(ipAddress)));
            });

    connect(NetworkDeviceManager::sharedInstance(),
            &NetworkDeviceManager::deviceAdded, this,
            [this](const NetworkDevice &device) {
                if (auto existingDevice =
                        AppContext::sharedInstance()->getDeviceByMacAddress(
                            device.macAddress)) {
                    if (existingDevice->deviceInfo.isWireless) {
                        qDebug() << "Ignoring wireless device with MAC:"
                                 << device.macAddress
                                 << "as it's already initialized";

                    } else {
                        qDebug() << "Prefering wired connection on device MAC:"
                                 << device.macAddress;
                    }

                    return;
                }
                qDebug() << "Trying to add network device with MAC:"
                         << device.macAddress;

                QMetaObject::invokeMethod(
                    AppContext::sharedInstance(), "addDevice",
                    Qt::QueuedConnection, Q_ARG(QString, device.macAddress),
                    Q_ARG(DeviceMonitorThread::IdeviceConnectionType,
                          DeviceMonitorThread::CONNECTION_NETWORK),
                    Q_ARG(AddType, AddType::Wireless),
                    Q_ARG(QString, device.macAddress),
                    Q_ARG(QString, device.address));
            });

    connect(AppContext::sharedInstance(), &AppContext::deviceHeartbeatFailed,
            this, [this](const QString &macAddress, int tries) {
                // Toast *toast = new Toast(this);
                // toast->setAttribute(Qt::WA_DeleteOnClose);
                // toast->setDuration(8000); // Hide after 8 seconds
                // toast->setTitle("Heartbeat failed");
                // toast->setText(
                //     QString("Heartbeat failed for device with MAC %1. "
                //             "Number of failed attempts: %2")
                //         .arg(macAddress)
                //         .arg(tries));
                // toast->setPosition(ToastPosition::BOTTOM_MIDDLE);
                // toast->show();
            });
}

void MainWindow::createMenus()
{
#ifdef Q_OS_MAC
    QMenu *actionsMenu = menuBar()->addMenu("&Actions");

    QAction *aboutAct = new QAction("&About iDescriptor", this);
    connect(aboutAct, &QAction::triggered, this, [this]() {
        QMessageBox::about(this, "iDescriptor",
                           "A free, open-source, and cross-platform "
                           "iDevice management tool.");
    });
    actionsMenu->addAction(aboutAct);
#endif
}

void MainWindow::updateNoDevicesConnected()
{
    qDebug() << "Is there no devices connected? "
             << AppContext::sharedInstance()->noDevicesConnected();
    if (AppContext::sharedInstance()->noDevicesConnected()) {

        m_connectedDeviceCountLabel->setText("iDescriptor: no devices");
        return m_mainStackedWidget->setCurrentIndex(0); // Show Welcome page
    }
    int deviceCount = AppContext::sharedInstance()->getConnectedDeviceCount();
    m_connectedDeviceCountLabel->setText(
        "iDescriptor: " + QString::number(deviceCount) +
        (deviceCount == 1 ? " device" : " devices") + " connected");
    m_mainStackedWidget->setCurrentIndex(1); // Show device list page
}

MainWindow::~MainWindow()
{
    // idevice_event_unsubscribe();
    // #ifdef ENABLE_RECOVERY_DEVICE_SUPPORT
    // irecv_device_event_unsubscribe(context);
    // #endif
    delete ui;
    m_deviceMonitor->requestInterruption();
    // FIXME:QThread: Destroyed while thread '' is still running
    // m_deviceMonitor->wait();
    delete m_deviceMonitor;
    // delete m_updater;
    // sleep(2);
}
