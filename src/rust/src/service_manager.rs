use crate::{APP_DEVICE_STATE, RUNTIME, run_sync, utils};
use cxx_qt::Threading;
use cxx_qt_lib::{QByteArray, QList, QMap, QMapPair_QString_QVariant, QString, QVariant};
use idevice::afc::opcode::AfcFopenMode;
use idevice::services::core_device_proxy::CoreDeviceProxy;
use idevice::{
    IdeviceService, RsdService, amfi,
    dvt::{location_simulation::LocationSimulationClient, remote_server::RemoteServerClient},
    installation_proxy::InstallationProxyClient,
    mobile_image_mounter::ImageMounter,
    provider::IdeviceProvider,
    rsd::RsdHandshake,
    simulate_location::LocationSimulationService,
    springboardservices::SpringBoardServicesClient,
};
use plist::Value;

use plist_macro::plist;
use serde_json;
use std::{io::Read, pin::Pin, time::Duration};

#[cxx_qt::bridge(namespace = "CXX")]
mod qobject {
    #[namespace = ""]
    unsafe extern "C++" {
        include!("cxx-qt-lib/qstring.h");
        include!("cxx-qt-lib/qlist.h");
        include!("cxx-qt-lib/qbytearray.h");
        include!("cxx-qt-lib/qmap.h");

        type QString = cxx_qt_lib::QString;
        type QList_QString = cxx_qt_lib::QList<QString>;
        type QByteArray = cxx_qt_lib::QByteArray;
        type QMap_QString_QVariant = cxx_qt_lib::QMap<cxx_qt_lib::QMapPair_QString_QVariant>;
    }

    extern "RustQt" {
        #[qobject]
        type ServiceManager = super::RServiceManager;

        #[qinvokable]
        fn set_udid(self: Pin<&mut ServiceManager>, udid: &QString);

        #[qinvokable]
        fn get_cable_info(&self);

        #[qinvokable]
        fn reveal_developer_mode_option_in_ui(&self);

        #[qinvokable]
        fn query_mobilegestalt(&self, keys: &QList_QString);

        #[qinvokable]
        fn mount_dev_image(&self, image_path: &QString, sig: &QString);

        #[qinvokable]
        fn get_mounted_image(&self);

        #[qinvokable]
        fn fetch_installed_apps(&self);

        #[qinvokable]
        fn set_location(&self, latitude: &QString, longitude: &QString) -> i32;

        #[qinvokable]
        fn fetch_app_icon(&self, bundle_id: QString);

        #[qsignal]
        fn cable_info_retrieved(self: Pin<&mut ServiceManager>, info: QString);

        #[qsignal]
        fn mobilegestalt_info_retrieved(
            self: Pin<&mut ServiceManager>,
            info: QMap_QString_QVariant,
        );

        #[qsignal]
        fn dev_image_mounted(self: Pin<&mut ServiceManager>, success: bool,is_locked: bool);

        #[qsignal]
        fn developer_mode_option_revealed(self: Pin<&mut ServiceManager>, success: bool);

        #[qsignal]
        fn mounted_image_retrieved(
            self: Pin<&mut ServiceManager>,
            success: bool,
            is_locked: bool,
            sig: QByteArray,
            sig_length: u64,
        );

        #[qsignal]
        fn installed_apps_retrieved(self: Pin<&mut ServiceManager>, apps: &QMap_QString_QVariant);

        #[qsignal]
        fn app_icon_loaded(
            self: Pin<&mut ServiceManager>,
            bundle_id: QString,
            icon_data: QByteArray,
        );

        #[qsignal]
        fn battery_info_updated(self: Pin<&mut ServiceManager>, info: &QString);

        #[qinvokable]
        fn fetch_disk_usage(&self, skip_gallery_usage: bool);

        #[qsignal]
        fn disk_usage_retrieved(
            self: Pin<&mut ServiceManager>,
            success: bool,
            apps_usage: u64,
            gallery_usage: u64,
        );

        #[qinvokable]
        fn restart(&self) -> bool;

        #[qinvokable]
        fn shutdown(&self) -> bool;

        #[qinvokable]
        fn enter_recovery_mode(&self) -> bool;

        #[qinvokable]
        fn install_ipa(&self, ipa_path: &QString);

        #[qsignal]
        fn install_ipa_init(self: Pin<&mut ServiceManager>, started: bool, state: QString);

        #[qsignal]
        fn install_ipa_progress(self: Pin<&mut ServiceManager>, progress: f64, state: QString);
    }

    impl cxx_qt::Threading for ServiceManager {}
    impl cxx_qt::Constructor<(QString, u32), NewArguments = (QString, u32)> for ServiceManager {}
}

#[derive(Default)]
pub struct RServiceManager {
    udid: QString,
    ios_version: u32,
}

impl cxx_qt::Constructor<(QString, u32)> for qobject::ServiceManager {
    type BaseArguments = ();
    type InitializeArguments = ();
    type NewArguments = (QString, u32);

    fn route_arguments(
        args: (QString, u32),
    ) -> (
        Self::NewArguments,
        Self::BaseArguments,
        Self::InitializeArguments,
    ) {
        (args, (), ())
    }

    fn new(args: (QString, u32)) -> RServiceManager {
        RServiceManager {
            udid: args.0,
            ios_version: args.1,
        }
    }

    fn initialize(self: Pin<&mut Self>, _arguments: Self::InitializeArguments) {
        let udid_for_log = self.get_udid().to_string();
        println!("ServiceManager::initialize called for UDID: {udid_for_log}");
        self.start_update_battery_info_interval();
    }
}

impl qobject::ServiceManager {
    pub fn start_update_battery_info_interval(self: Pin<&mut Self>) {
        let qt_thread = self.qt_thread();
        let udid = self.get_udid().to_string();
        println!("Starting battery info update interval for device {udid}");
        RUNTIME.spawn(async move {
            let mut interval = tokio::time::interval(Duration::from_secs(30));

            loop {
                interval.tick().await;

                let maybe_device = APP_DEVICE_STATE.lock().await.get(udid.as_str()).cloned();

                let device = match maybe_device {
                    Some(d) => d,
                    None => {
                        println!("Battery info interval: Device {udid} not found");
                        return;
                    }
                };

                println!("Battery info interval: Fetching battery info for device {udid}");

                utils::get_battery_info(&mut *device.diag.lock().await)
                    .await
                    .map(|info| {
                        let mut buf = Vec::new();
                        if Value::Dictionary(info).to_writer_xml(&mut buf).is_ok() {
                            if let Ok(s) = String::from_utf8(buf) {
                                qt_thread
                                    .queue(move |t| {
                                        t.battery_info_updated(&QString::from(s));
                                    })
                                    .ok();
                            }
                        }
                    });
            }
        });
    }

    fn get_udid(&self) -> &QString {
        use cxx_qt::CxxQtType;
        &self.rust().udid
    }

    fn get_ios_version(&self) -> u32 {
        use cxx_qt::CxxQtType;
        self.rust().ios_version
    }

    fn set_udid(mut self: Pin<&mut Self>, udid: &QString) {
        use cxx_qt::CxxQtType;
        self.as_mut().rust_mut().udid = udid.clone();
    }

    fn query_mobilegestalt(&self, keys: &QList<QString>) {
        let udid = self.get_udid().to_string();
        let qt_t = self.qt_thread();
        let keys_owned = keys.clone();
        RUNTIME.spawn(async move {
            let mut map = QMap::<QMapPair_QString_QVariant>::default();

            let keys_vec: Vec<String> = keys_owned
                .into_iter()
                .map(|qstr| qstr.to_string())
                .collect();
            let qt_thread = qt_t.clone();
            let maybe_device = APP_DEVICE_STATE.lock().await.get(udid.as_str()).cloned();

            let device = match maybe_device {
                Some(d) => d,
                None => {
                    eprintln!("query_mobilegestalt: device {udid} not found");
                    let _ = qt_thread.queue(move |t| {
                        t.mobilegestalt_info_retrieved(map);
                    });
                    return;
                }
            };

            let result = device.diag.lock().await.mobilegestalt(Some(keys_vec)).await;

            match result {
                Ok(opt_dict) => {
                    if let Some(mut root_dict) = opt_dict {
                         let mobilegestalt_value = root_dict.remove("MobileGestalt");

                         let inner_mobilegestalt_dict = match mobilegestalt_value {
                            Some(Value::Dictionary(dict)) => dict, 
                            _ => {
                                eprintln!(
                                    "query_mobilegestalt: MobileGestalt key not found or not a dictionary for device {udid}."
                                );
                                let _ = qt_thread.queue(move |t| {
                                    t.mobilegestalt_info_retrieved(map);
                                });
                                return;
                            }
                        };

                        for (k, v) in inner_mobilegestalt_dict.into_iter() {
                            let v_str = format!("{v:?}");
                            map.insert(QString::from(k), QVariant::from(&QString::from(v_str)));
                        }
                    }
                    let _ = qt_thread.queue(move |t| {
                        t.mobilegestalt_info_retrieved(map);
                    });
                }
                Err(e) => {
                    eprintln!(
                        "query_mobilegestalt: error querying MobileGestalt for device {udid}: {e}"
                    );
                    let _ = qt_thread.queue(move |t| {
                        t.mobilegestalt_info_retrieved(map);
                    });
                }
            }
        });
    }

    fn get_cable_info(&self) {
        let udid = self.get_udid().to_string();
        let qt_t = self.qt_thread();

        RUNTIME.spawn(async move {
            let qt_thread = qt_t.clone();
            let maybe_device = APP_DEVICE_STATE
                .lock()
                .await
                .get(udid.as_str())
                .cloned();

            let device = match maybe_device {
                Some(d) => d,
                None => {
                    eprintln!("get_cable_info: device {udid} not found");
                    let _ = qt_thread.queue(|t| {
                        t.cable_info_retrieved(QString::from(""));
                    });
                    return;
                }
            };

            let res = utils::get_cable_info(&mut *device.diag.lock().await).await;

            match res {
                Some(dict) => {
                    let mut buf = Vec::new();
                    if Value::Dictionary(dict).to_writer_xml(&mut buf).is_err() {
                        eprintln!(
                            "get_cable_info: Failed to serialize ioregistry values to XML for device {udid}."
                        );
                        let _ = qt_thread.queue(|t| {
                            t.cable_info_retrieved(QString::from(""));
                        });
                        return;
                    }

                    match String::from_utf8(buf) {
                        Ok(s) => {
                            let _ = qt_thread.queue(move |t| {
                                t.cable_info_retrieved(QString::from(s));
                            });
                        }
                        Err(_) => {
                            eprintln!(
                                "get_cable_info: Failed to convert ioregistry XML data to string for device {udid}."
                            );
                            let _ = qt_thread.queue(|t| {
                                t.cable_info_retrieved(QString::from(""));
                            });
                        }
                    }
                }
                None => {
                    eprintln!("get_cable_info: Failed to get ioregistry for device {udid}");
                    let _ = qt_thread.queue(|t| {
                        t.cable_info_retrieved(QString::from(""));
                    });
                }
            }
        });
    }
    fn mount_dev_image(&self, image_path: &QString, sig: &QString) {
        let udid = self.get_udid().to_string();
        let qt_t = self.qt_thread();
        let image = image_path.to_string();
        let signature = sig.to_string();

        RUNTIME.spawn(async move {
            let qt_thread = qt_t.clone();
            let maybe_device = APP_DEVICE_STATE
                .lock()
                .await
                .get(udid.as_str())
                .cloned();

            let device = match maybe_device {
                Some(d) => d,
                None => {
                    eprintln!("get_cable_info: device {udid} not found");
                    let _ = qt_thread.queue(|t| {
                        t.dev_image_mounted(false,false);
                    });
                    return;
                }
            };

            let mut mounter = match {
                let provider_guard = device.provider.lock().await;
                let provider_ref: &dyn IdeviceProvider = provider_guard.as_ref();
                ImageMounter::connect(provider_ref).await
            } {
                Ok(m) => m,
                Err(e) => {
                    eprintln!("mount_dev_image: Failed to connect to ImageMounter for device {udid}: {e}");
                    let _ = qt_thread.queue(|t| {
                        t.dev_image_mounted(false,false);
                    });
                    return;
                }
            };

            let mut file = match std::fs::File::open(&image) {
                Ok(f) => f,
                Err(e) => {
                    eprintln!("mount_dev_image: Failed to open image file {image} for device {udid}: {e}");
                    let _ = qt_thread.queue(|t| {
                        t.dev_image_mounted(false,false);
                    });
                    return;
                }
            };
            let mut buf = Vec::new();
            if let Err(e) = file.read_to_end(&mut buf) {
                eprintln!("mount_dev_image: Failed to read image file {image} for device {udid}: {e}");
                let _ = qt_thread.queue(|t| {
                    t.dev_image_mounted(false,false);
                });
                return;
            }

            let mut sig_file = match std::fs::File::open(&signature) {
                Ok(f) => f,
                Err(e) => {
                    eprintln!("mount_dev_image: Failed to open signature file {signature} for device {udid}: {e}");
                    let _ = qt_thread.queue(|t| {
                        t.dev_image_mounted(false,false);
                    });
                    return;
                }
            };

            let mut sig_buf: Vec<u8> = Vec::new();
            if let Err(e) = sig_file.read_to_end(&mut sig_buf) {
                eprintln!("mount_dev_image: Failed to read signature file {signature} for device {udid}: {e}");
                let _ = qt_thread.queue(|t| {
                    t.dev_image_mounted(false,false);
                });
                return;
            }

            match mounter.mount_developer(&buf, sig_buf).await {
                Ok(_) => {
                    let _ = qt_thread.queue(|t| {
                        t.dev_image_mounted(true ,false);
                    });
                }
                Err(idevice::IdeviceError::DeviceLocked) => {
                    eprintln!("mount_dev_image: Failed to mount developer image for device {udid}: device locked");
                    let _ = qt_thread.queue(|t| {
                        t.dev_image_mounted(false,true);
                    }).ok(); 
                }
                Err(e) => {
                    eprintln!("mount_dev_image: Failed to mount developer image for device {udid}: {e}");
                    let _ = qt_thread.queue(|t| {
                        t.dev_image_mounted(false,false);
                    }).ok();
                }
            };

        });
    }

    fn get_mounted_image(&self) {
        let udid = self.get_udid().to_string();
        let qt_t = self.qt_thread();

        RUNTIME.spawn(async move {
            let qt_thread = qt_t.clone();
            let maybe_device = APP_DEVICE_STATE
                .lock()
                .await
                .get(udid.as_str())
                .cloned();

            let device = match maybe_device {
                Some(d) => d,
                None => {
                    eprintln!("get_mounted_image: device {udid} not found");
                    let _ = qt_thread.queue(|t| {
                        t.mounted_image_retrieved(false,false,QByteArray::default(), 0);
                    }).ok();
                    return;
                }
            };

            let mut mounter = match {
                let provider_guard = device.provider.lock().await;
                let provider_ref: &dyn IdeviceProvider = provider_guard.as_ref();
                ImageMounter::connect(provider_ref).await
            } {
                Ok(m) => m,
                Err(e) => {
                    eprintln!("get_mounted_image: Failed to connect to ImageMounter for device {udid}: {e}");
                    let _ = qt_thread.queue(|t| {
                        t.mounted_image_retrieved(false,false,QByteArray::default(), 0);
                    }).ok();
                    return;
                }
            };

            match mounter.lookup_image("Developer").await {
                Ok(res) => {
                    let _  = qt_thread.queue(move|t| {
                        t.mounted_image_retrieved(true,false,QByteArray::from(&res[..]), res.len() as u64);
                    }).ok();
                }
                Err(idevice::IdeviceError::DeviceLocked) => {
                    eprintln!("get_mounted_image: Failed to lookup mounted developer image for device {udid}: device locked");
                    let _ = qt_thread.queue(|t| {
                        t.mounted_image_retrieved(false,true,QByteArray::default(), 0);
                    }).ok(); 
                }
                Err(idevice::IdeviceError::NotFound) => {
                    eprintln!("get_mounted_image: No mounted developer image found for device {udid}");
                    let _ = qt_thread.queue(|t| {
                        t.mounted_image_retrieved(true,false,QByteArray::default(), 0);
                    }).ok();
                }
                Err(e) => {
                    eprintln!("get_mounted_image: Failed to lookup mounted developer image for device {udid}: {e}");
                    let _ = qt_thread.queue(|t| {
                        t.mounted_image_retrieved(false,false,QByteArray::default(), 0);
                    }).ok();
                }
            };

        });
    }

    fn reveal_developer_mode_option_in_ui(&self) {
        let udid = self.get_udid().to_string();
        let qt_t = self.qt_thread();

        RUNTIME.spawn(async move {
            let qt_thread = qt_t.clone();
            
            let mut amfi_client = match {
                let maybe_device = APP_DEVICE_STATE
                    .lock()
                    .await
                    .get(udid.as_str())
                    .cloned();
    
                let device = match maybe_device {
                    Some(d) => d,
                    None => {
                        eprintln!("get_cable_info: device {udid} not found");
                        let _ = qt_thread.queue(|t| {
                            t.developer_mode_option_revealed(false);
                        }).ok();
                        return;
                    }
                };
                let provider_guard = device.provider.lock().await;
                let provider_ref: &dyn IdeviceProvider = provider_guard.as_ref();
                amfi::AmfiClient::connect(provider_ref).await
            } {
                Ok(c) => c,
                Err(e) => {
                    eprintln!("reveal_developer_mode_option_in_ui: Failed to connect to AMFI service for device {udid}: {e}");
                    let _ = qt_thread.queue(|t| {
                        t.developer_mode_option_revealed(false);
                    }).ok();
                    return;
                }
            };


            match amfi_client.reveal_developer_mode_option_in_ui().await {
                Ok(_) => {
                    let _ = qt_thread.queue(|t| {
                        t.developer_mode_option_revealed(true);
                    }).ok();
                }
                Err(e) => {
                    eprintln!("reveal_developer_mode_option_in_ui: Failed to reveal developer mode option in UI for device {udid}: {e}");
                    let _ = qt_thread.queue(|t| {
                        t.developer_mode_option_revealed(false);
                    }).ok();
            }

            }
        });
    }

    fn fetch_installed_apps(&self) {
        let udid = self.get_udid().to_string();
        let qt_t = self.qt_thread();

        RUNTIME.spawn(async move {
            let qt_thread = qt_t.clone();
            let maybe_device = APP_DEVICE_STATE
                .lock()
                .await
                .get(udid.as_str())
                .cloned();

            let mut all_apps = QMap::<QMapPair_QString_QVariant>::default();

            let device = match maybe_device {
                Some(d) => d,
                None => {
                    eprintln!("fetch_installed_apps: device {udid} not found");
                    let _ = qt_thread.queue(move |t| {
                        t.installed_apps_retrieved(&all_apps);
                    });
                    return;
                }
            };

            let mut ins_client = match {
                let provider_guard = device.provider.lock().await;
                let provider_ref: &dyn IdeviceProvider = provider_guard.as_ref();
                InstallationProxyClient::connect(provider_ref).await
            } {
                Ok(c) => c,
                Err(e) => {
                    eprintln!("fetch_installed_apps: Failed to connect to InstallationProxy service for device {udid}: {e}");
                    let _ = qt_thread.queue( move |t| {
                        t.installed_apps_retrieved(&all_apps);
                    });
                    return;
                }
            };

            // Get both User and System apps
            for app_type in ["User", "System"] {
                let client_options = plist!({
                    "ApplicationType": app_type,
                    "ReturnAttributes": [
                        "CFBundleIdentifier",
                        "CFBundleDisplayName",
                        "CFBundleShortVersionString",
                        "CFBundleVersion",
                        "UIFileSharingEnabled"
                    ]
                });

                let apps = match ins_client.browse(Some(client_options)).await {
                    Ok(apps) => apps,
                    Err(e) => {
                        eprintln!("fetch_installed_apps: Failed to browse installed apps for device {udid} and app type {app_type}: {e}");
                        continue; // Skip this app type 
                    }
                };

                for app_info in apps {
                    if let plist::Value::Dictionary(app_dict) = app_info {
                        let bundle_id = app_dict
                            .get("CFBundleIdentifier")
                            .and_then(|v| v.as_string())
                            .unwrap_or_default();

                        if bundle_id.is_empty() {
                            continue;
                        }

                        let display = app_dict
                            .get("CFBundleDisplayName")
                            .and_then(|v| v.as_string())
                            .unwrap_or_default();
                        let version = app_dict
                            .get("CFBundleShortVersionString")
                            .and_then(|v| v.as_string())
                            .unwrap_or_default();
                        let fs_enabled = app_dict
                            .get("UIFileSharingEnabled")
                            .and_then(|v| v.as_boolean())
                            .unwrap_or(false);

                        let app_json = format!(
                            "{{\"bundle_id\":{},\"CFBundleDisplayName\":{},\"CFBundleShortVersionString\":{},\"UIFileSharingEnabled\":{},\"app_type\":{}}}",
                            serde_json::to_string(&bundle_id).unwrap_or_else(|_| format!("\"{}\"", bundle_id)),
                            serde_json::to_string(&display).unwrap_or_else(|_| format!("\"{}\"", display)),
                            serde_json::to_string(&version).unwrap_or_else(|_| format!("\"{}\"", version)),
                            fs_enabled,
                            serde_json::to_string(&app_type).unwrap_or_else(|_| format!("\"{}\"", app_type)),
                        );

                        all_apps.insert(
                            QString::from(bundle_id),
                            QVariant::from(&QString::from(app_json)),
                        );
                    }
                }
            }

            let _ = qt_thread.queue(move |t| {
                t.installed_apps_retrieved(&all_apps);
            });
        });
    }

    fn fetch_app_icon(&self, bundle_id: QString) {
        let udid = self.get_udid().to_string();
        let qt_t = self.qt_thread();
        let bundle_id_owned = bundle_id.clone();

        RUNTIME.spawn(async move {
            let maybe_device = APP_DEVICE_STATE
                .lock()
                .await
                .get(udid.as_str())
                .cloned();

            let device = match maybe_device {
                Some(d) => d,
                None => {
                    eprintln!("fetch_app_icon: device {udid} not found");
                    return;
                }
            };

            let mut springboard_client = match {
                let provider_guard = device.provider.lock().await;
                let provider_ref: &dyn IdeviceProvider = provider_guard.as_ref();
                SpringBoardServicesClient::connect(provider_ref).await
            } {
                Ok(c) => c,
                Err(e) => {
                    eprintln!("fetch_app_icon: Failed to connect to InstallationProxy service for device {udid}: {e}");
                    return;
                }
            };

            match springboard_client.get_icon_pngdata(bundle_id_owned.to_string()).await {
                Ok(icon_data) => {
                    qt_t.queue(move |t| {
                        t.app_icon_loaded(bundle_id_owned, QByteArray::from(&icon_data[..]));
                    }).ok();
                }
                Err(e) => {
                    eprintln!("fetch_app_icon: Failed to fetch app icon for bundle ID {} on device {udid}: {e}", bundle_id_owned);
                }
            };
        });
    }

    fn set_location(&self, latitude: &QString, longitude: &QString) -> i32 {
        let udid = self.get_udid().to_string();
        let ios_version = self.get_ios_version();

        /*
            FIXME: use RUNTIME.spawn in the future
        */
        RUNTIME.block_on(async move {
            tokio::select! {
                res = async {
                    let maybe_device = APP_DEVICE_STATE
                        .lock()
                        .await
                        .get(udid.as_str())
                        .cloned();

                    let device = match maybe_device {
                        Some(d) => d,
                        None => {
                            eprintln!("set_location: device {udid} not found");
                            return -1;
                        }
                    };

                    let result = {
                        let provider_guard = device.provider.lock().await;

                        if ios_version < 17 {
                            set_device_location_lockdown( provider_guard.as_ref(), latitude.to_string().as_str(), longitude.to_string().as_str()).await
                        } else {
                            println!("Using RSD path for setting location on device {udid} with iOS version {ios_version}");
                            let proxy_res = {
                                CoreDeviceProxy::connect(provider_guard.as_ref()).await
                            };

                            match proxy_res {
                                Ok(proxy) => set_device_location_rsd(proxy, utils::qstring_to_f64(latitude).unwrap_or(0.0), utils::qstring_to_f64(longitude).unwrap_or(0.0)).await,
                                Err(err) => {
                                    println!("Failed to connect to CoreDeviceProxy for device {udid}, cannot set location using RSD path.");
                                    return err.code();
                                },
                            }
                        }
                    };

                    match result {
                        Ok(_) => 0,
                        Err(e) => {
                            eprintln!(
                                "set_location: failed to set virtual location for device {udid}: {e:?}"
                            );
                            return e.code();
                        }
                    }
                } => {
                    res
                }
                _ = tokio::time::sleep(Duration::from_secs(10)) => {
                    eprintln!("set_location: timed out");
                    -71
                }
            }
        })
    }

    fn fetch_disk_usage(&self, skip_gallery_usage: bool) {
        let udid = self.get_udid().to_string();
        let qt_t = self.qt_thread();

        RUNTIME.spawn(async move {
            let qt_thread = qt_t.clone();

            let mut instproxy = {                
                let maybe_device = APP_DEVICE_STATE
                    .lock()
                    .await
                    .get(udid.as_str())
                    .cloned();
    
                let device = match maybe_device {
                    Some(d) => d,
                    None => {
                        eprintln!("fetch_disk_usage: device {udid} not found");
                        return;
                    }
                };


                match InstallationProxyClient::connect(device.provider.lock().await.as_ref()).await {
                    Ok(c) => c,
                    Err(e) => {
                        eprintln!("fetch_disk_usage: Failed to connect to InstallationProxy service for device {udid}: {e}");
                        qt_thread.queue(move |t| {
                            t.disk_usage_retrieved(false, 0, 0);
                        }).ok();
                        return;
                    }
                }
            };
            

            match utils::calculate_apps_usage(&mut instproxy).await {
                Ok(apps_usage) => {
                    
                    if skip_gallery_usage {
                        qt_thread.queue(move |t| {
                            t.disk_usage_retrieved(true, apps_usage, 0);
                        }).ok();
                        return;
                    }
                    let maybe_device = APP_DEVICE_STATE
                    .lock()
                    .await
                    .get(udid.as_str())
                    .cloned();
    
                    let device = match maybe_device {
                        Some(d) => d,
                        None => {
                            eprintln!("fetch_disk_usage: device {udid} not found");
                            return;
                        }
                    };

                    let mut afc = device.afc.lock().await;

                    // FIXME: go safer here
                    let mut fd = afc.open("/PhotoData/Photos.sqlite", AfcFopenMode::RdOnly).await.map_err(|e| {
                        eprintln!("fetch_disk_usage: Failed to read gallery database for device {udid}: {e}");
                        e
                     }).unwrap();

                    let mut gallery_db_bytes = fd.read_entire().await.unwrap();


                    match utils::query_gallery_usage(&mut gallery_db_bytes) {
                        Ok(gallery_usage) => {
                            qt_thread.queue(move |t| {
                                t.disk_usage_retrieved(true, apps_usage, gallery_usage);
                            }).ok();
                        }
                        Err(e) => {
                            eprintln!("fetch_disk_usage: Failed to calculate gallery disk usage for device {udid}: {e}");
                            qt_thread.queue(move |t| {
                                t.disk_usage_retrieved(true, apps_usage, 0);
                            }).ok();
                        }
                    }

                    
                }
                Err(e) => {
                    eprintln!("fetch_disk_usage: Failed to calculate apps disk usage for device {udid}: {e}");
                    qt_thread.queue(move |t| {
                        t.disk_usage_retrieved(false, 0, 0);
                    }).ok();
                }
            };
        

        });
    }

    fn restart(&self) -> bool {
        let udid = self.get_udid().to_string();

        run_sync(async move {
            let maybe_device = APP_DEVICE_STATE
                .lock()
                .await
                .get(udid.as_str())
                .cloned();

            let device = match maybe_device {
                Some(d) => d,
                None => {
                    eprintln!("restart: device {udid} not found");
                    return false;
                }
            };

            if let Err(e) = device.diag.lock().await.restart().await {
                eprintln!("restart: Failed to restart device {udid}: {e}");
                return false
            }
            return true;
        })
    }

    fn shutdown(&self) -> bool {
        let udid = self.get_udid().to_string();

        run_sync(async move {
            let maybe_device = APP_DEVICE_STATE
                .lock()
                .await
                .get(udid.as_str())
                .cloned();

            let device = match maybe_device {
                Some(d) => d,
                None => {
                    eprintln!("shutdown: device {udid} not found");
                    return false;
                }
            };

            if let Err(e) = device.diag.lock().await.shutdown().await {
                eprintln!("shutdown: Failed to shutdown device {udid}: {e}");
                return false
            }
            return true;
        })
    }
    fn enter_recovery_mode(&self) -> bool {
        let udid = self.get_udid().to_string();

        run_sync(async move {
            let maybe_device = APP_DEVICE_STATE
                .lock()
                .await
                .get(udid.as_str())
                .cloned();

            let device = match maybe_device {
                Some(d) => d,
                None => {
                    eprintln!("enter_recovery_mode: device {udid} not found");
                    return false;
                }
            };

            if let Err(e) = device.lockdown.lock().await.enter_recovery().await {
                eprintln!("enter_recovery_mode: Failed to enter recovery mode for device {udid}: {e}");
                return false
            }
            return true;
        })
    }

    fn install_ipa(&self, local_ipa_path: &QString) {
        let udid = self.get_udid().to_string();
        let qt_t = self.qt_thread();
        let local_ipa_path_owned = local_ipa_path.clone().to_string();
        // FIXME: this is a bit hacky 
        let ipa_path_on_device = format!("/PublicStaging/{}", local_ipa_path.to_string().split('/').last().unwrap_or("app.ipa"));
        RUNTIME.spawn(async move {
            let qt_thread = qt_t.clone();
            
            let mut ins_client = match {
                let maybe_device = APP_DEVICE_STATE
                    .lock()
                    .await
                    .get(udid.as_str())
                    .cloned();
    
                let device = match maybe_device {
                    Some(d) => d,
                    None => {
                        eprintln!("install_ipa: device {udid} not found");
                        qt_thread.queue(move |t| {
                            t.install_ipa_init(false, QString::from("Device not found"));
                        }).ok();
                        return;
                    }
                };

                let mut afc = device.afc.lock().await;
                
                // Create the staging directory
                match utils::ensure_public_staging(&mut afc).await {
                    Ok(_) => (),
                    Err(e) => {
                        eprintln!("install_ipa: Failed to ensure /PublicStaging directory exists on device {udid}: {e}");
                        qt_thread.queue(move |t| {
                            t.install_ipa_init(false, QString::from("Failed to prepare device for IPA upload"));
                        }).ok();
                        return;
                    }
                };

                match std::fs::exists(&local_ipa_path_owned)  {
                    Ok(true) => (),
                    Ok(false) => {
                        eprintln!("install_ipa: IPA file not found at path {local_ipa_path_owned}");
                        qt_thread.queue(move |t| {
                            t.install_ipa_init(false, QString::from("IPA file not found"));
                        }).ok();
                        return;
                    }
                    Err(e) => {
                        eprintln!("install_ipa: Failed to check if IPA file exists at path {local_ipa_path_owned}: {e}");
                        qt_thread.queue(move |t| {
                            t.install_ipa_init(false, QString::from("Failed to access IPA file"));
                        }).ok();
                        return;
                    }
                }



                match afc.open(&ipa_path_on_device, AfcFopenMode::WrOnly).await {
                    Ok(mut fd) => {
                        let mut local_file = match std::fs::File::open(&local_ipa_path_owned) {
                            Ok(f) => f,
                            Err(e) => {
                                eprintln!("install_ipa: Failed to open local IPA file for device {udid}: {e}");
                                qt_thread.queue(move |t| {
                                    t.install_ipa_init(false, QString::from("Failed to open local IPA file"));
                                }).ok();
                                return;
                            }
                        };

                        let mut file_btytes = Vec::new();
                        match local_file.read_to_end(&mut file_btytes) {
                            Ok(bytes) => bytes,
                            Err(e) => {
                                eprintln!("install_ipa: Failed to read local IPA file for device {udid}: {e}");
                                qt_thread.queue(move |t| {
                                    t.install_ipa_init(false, QString::from("Failed to read local IPA file"));
                                }).ok();
                                return;
                            }
                        };

                        if let Err(e) = fd.write_entire(&file_btytes).await {
                            eprintln!("install_ipa: Failed to upload IPA to device {udid}: {e}");
                            qt_thread.queue(move |t| {
                                t.install_ipa_init(false, QString::from("Failed to upload IPA to device"));
                            }).ok();
                            return;
                        }
                    }
                    Err(e) => {
                        eprintln!("install_ipa: Failed to create file on device {udid} for IPA upload: {e}");
                        qt_thread.queue(move |t| {
                            t.install_ipa_init(false, QString::from("Failed to create file on device for IPA upload"));
                        }).ok();
                        return;
                    }
                }



                let provider_guard = device.provider.lock().await;
                let provider_ref: &dyn IdeviceProvider = provider_guard.as_ref();
                InstallationProxyClient::connect(provider_ref).await
            } {
                Ok(c) => c,
                Err(e) => {
                    eprintln!("install_ipa: Failed to connect to InstallationProxy service for device {udid}: {e}");
                    qt_thread.queue(move |t| {
                        t.install_ipa_init(false, QString::from("Failed to connect to Installation Proxy"));
                    }).ok();
                    return;
                }
            };

            qt_thread.queue(move |t| {
                t.install_ipa_init(true, QString::from("Connected to Installation Proxy"));
            }).ok();

            let state = String::from("Installing IPA");

            let res = ins_client
                .install_with_callback(
                    ipa_path_on_device,
                    None,
                    move |(percent, state)| {
                        let qt_thread = qt_thread.clone();
                        async move {
                            let progress = percent as f64 / 100.0;

                            qt_thread
                                .queue(move |t| {
                                    t.install_ipa_progress(
                                        progress,
                                        QString::from(&state),
                                    );
                                })
                                .ok();

                            println!(
                                "Installation progress: {percent}%"
                            );
                        }
                    },
                    state,
                )
                .await;

            if let Err(e) = res {
                eprintln!("install_ipa: Failed to install IPA on device {udid}: {e}");
            } else {
                println!("install_ipa: Successfully initiated installation on device {udid}");
            }
        });
    }
}

async fn set_device_location_lockdown(
    provider: &dyn IdeviceProvider,
    latitude: &str,
    longitude: &str,
) -> Result<(), idevice::IdeviceError> {
    let mut client = LocationSimulationService::connect(provider).await?;
    client.set(latitude, longitude).await
}

// iOS 17+:
async fn set_device_location_rsd(
    proxy: CoreDeviceProxy,
    latitude: f64,
    longitude: f64,
) -> Result<(), idevice::IdeviceError> {
    let rsd_port = proxy.handshake.server_rsd_port;
    let adapter = proxy.create_software_tunnel()?;
    let mut adapter = adapter.to_async_handle();
    let stream = adapter.connect(rsd_port).await?;

    let mut handshake = RsdHandshake::new(stream).await?;

    let mut remote_server = RemoteServerClient::connect_rsd(&mut adapter, &mut handshake).await?;
    remote_server.read_message(0).await?;

    let mut location_client = LocationSimulationClient::new(&mut remote_server).await?;
    location_client.set(latitude, longitude).await
}
