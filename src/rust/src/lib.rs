use futures::StreamExt;
use idevice::{
    IdeviceError, IdeviceService,
    afc::AfcClient,
    diagnostics_relay::DiagnosticsRelayClient,
    heartbeat,
    lockdown::LockdownClient,
    pairing_file::PairingFile,
    provider::TcpProvider,
    usbmuxd::{Connection, UsbmuxdAddr, UsbmuxdConnection, UsbmuxdListenEvent},
};
use std::{any::type_name, sync::Arc};
use std::{collections::HashMap, net::IpAddr};
use tokio::sync::Mutex;

use std::future::Future;
use std::sync::mpsc;
use std::thread;
use tokio::runtime::{Builder, Runtime};
use tokio::sync::oneshot;
use tokio::task::JoinHandle;

use core::pin::Pin;
use cxx_qt::Threading;
use cxx_qt_lib::{QMap, QMapPair_QString_QVariant, QString, QVariant};

use crate::qobject::Core;
use once_cell::sync::Lazy;
use plist::Value;
mod afc;
mod afc2_services;
mod afc_services;
mod hause_arrest;
mod io_manager;
mod screenshot;
mod service_manager;
mod utils;

const POSSIBLE_ROOT: &str = "../../../../";
const APP_LABEL: &str = "iDescriptor";
const EV_CONNECTED: u32 = 1;
const EV_DISCONNECTED: u32 = 2;
const EV_PAIRING_PENDING: u32 = 3;
const EV_FAIL: u32 = 4;

#[derive(Clone)]
pub struct DeviceServices {
    pub afc: Arc<Mutex<AfcClient>>,
    pub afc2: Option<Arc<Mutex<AfcClient>>>,
    pub diag: Arc<Mutex<DiagnosticsRelayClient>>,
    pub heartbeat_task: Option<Arc<JoinHandle<()>>>,
    pub video_streams: Arc<Mutex<HashMap<String, oneshot::Sender<()>>>>,
    pub provider: Arc<Mutex<Box<dyn idevice::provider::IdeviceProvider>>>,
    pub lockdown: Arc<Mutex<LockdownClient>>,
}

pub static APP_DEVICE_STATE: Lazy<Mutex<HashMap<String, DeviceServices>>> =
    Lazy::new(|| Mutex::new(HashMap::new()));

static RUNTIME: Lazy<Runtime> = Lazy::new(|| {
    tokio::runtime::Builder::new_multi_thread()
        .enable_all()
        .build()
        .unwrap()
});

pub fn run_sync<F, R>(fut: F) -> R
where
    F: Future<Output = R> + Send + 'static,
    R: Send + 'static,
{
    let (tx, rx) = mpsc::sync_channel(1);

    RUNTIME.spawn(async move {
        let res = fut.await;
        let _ = tx.send(res);
    });

    rx.recv().expect("Tokio runtime worker panicked")
}

#[cxx_qt::bridge(namespace = "CXX")]
mod qobject {
    #[namespace = ""]
    unsafe extern "C++" {
        include!("cxx-qt-lib/qstring.h");
        include!("cxx-qt-lib/qlist.h");
        include!("cxx-qt-lib/qmap.h");

        type QString = cxx_qt_lib::QString;
        type QMap_QString_QVariant = cxx_qt_lib::QMap<cxx_qt_lib::QMapPair_QString_QVariant>;
    }

    extern "RustQt" {
        #[qobject]
        type Core = super::RCore;

        #[qinvokable]
        fn init(self: Pin<&mut Core>);

        #[qinvokable]
        fn init_wireless_device(
            self: Pin<&mut Core>,
            ip: &QString,
            pairing_file: &QString,
            mac_address: &QString,
        );

        #[qinvokable]
        fn get_pairing_files(self: Pin<&mut Core>) -> QMap_QString_QVariant;

        #[qinvokable]
        fn remove_device(self: Pin<&mut Core>, udid: &QString);

        #[qsignal]
        fn device_event(self: Pin<&mut Core>, event_type: u32, udid: &QString, info: &QString);

        #[qsignal]
        fn init_failed(self: Pin<&mut Core>, mac_address: &QString);

        #[qsignal]
        fn no_pairing_file(self: Pin<&mut Core>, mac_address: &QString);

        #[qsignal]
        fn sleepy_time_detected(self: Pin<&mut Core>);

        #[qsignal]
        fn device_became_wired(self: Pin<&mut Core>, udid: &QString);
    }
    impl cxx_qt::Threading for Core {}
}

#[derive(Default)]
pub struct RCore;

impl qobject::Core {
    fn init(self: Pin<&mut Self>) {
        self.listen();
    }

    fn listen(self: Pin<&mut Self>) {
        let qt_t = self.qt_thread();

        thread::spawn(move || {
            let rt = Builder::new_current_thread().enable_all().build().unwrap();
            rt.block_on(async move {
                let mut device_map: HashMap<u32, String> = HashMap::new();

                loop {
                    match UsbmuxdConnection::default().await {
                        Ok(mut uc) => match uc.listen().await {
                            Ok(mut stream) => {
                                while let Some(evt) = stream.next().await {
                                    match evt {
                                        Ok(UsbmuxdListenEvent::Connected(d)) => {
                                            // ignore non-USB connections
                                            if d.connection_type != Connection::Usb {
                                                continue;
                                            }

                                            let udid = d.udid.clone();
                                            let device_id = d.device_id;
                                            device_map.insert(device_id, udid.clone());

                                            let qt_thread = qt_t.clone();
                                            RUNTIME.spawn(async move {
                                                let pair_record_exists = {
                                                    let mut u2 =
                                                        match UsbmuxdConnection::default().await {
                                                            Ok(u) => u,
                                                            Err(_) => return,
                                                        };

                                                    match u2.get_pair_record(&udid).await {
                                                        Ok(_) => true,
                                                        Err(_) => false,
                                                    }
                                                };


                                                // we may not even need to check if pair record exists
                                                if pair_record_exists {
                                                    emit_connected(qt_thread.clone(), udid).await;
                                                    return;
                                                }
                                                
                                                match handle_pairing(qt_thread.clone(), udid.clone()).await {
                                                    Ok(_) => {
                                                        emit_connected(qt_thread.clone(), udid).await;
                                                    }
                                                    Err(e) => {
                                                        eprintln!("Pairing failed for device {}: {e:?}", udid);
                                                        emit_pairing_failed(qt_thread.clone(), udid, "Failed to pair device");
                                                    }
                                                }

                                            });
                                        }
                                        /* DISCONNECTED */
                                        Ok(UsbmuxdListenEvent::Disconnected(device_id)) => {
                                            if let Some(udid) = device_map.remove(&device_id) {
                                                clean_device_from_app_state(&udid).await;

                                                let qt_thread = qt_t.clone();
                                                qt_thread
                                                    .queue(move |core_qobj| {
                                                        core_qobj.device_event(
                                                            EV_DISCONNECTED,
                                                            &QString::from(udid),
                                                            &QString::from(""),
                                                        );
                                                    })
                                                    .ok();
                                            }
                                        }
                                        Err(e) => {
                                            eprintln!("usbmuxd listen error: {e:?}");
                                            break;
                                        }
                                    }
                                }
                            }
                            Err(e) => eprintln!("Failed to start usbmuxd listen: {e:?}"),
                        },
                        Err(_) => {}
                    }

                    tokio::time::sleep(std::time::Duration::from_millis(2000)).await;
                }
            });
        });
    }

    fn init_wireless_device(
        self: Pin<&mut Self>,
        ip: &QString,
        pairing_file: &QString,
        mac_address: &QString,
    ) {
        eprintln!(
            "init_wireless_device: MAC: {} IP: {} PairingFile: {}",
            mac_address, ip, pairing_file
        );
        let qt_t = self.qt_thread();
        let ip_owned = ip.to_string();
        let pairing_path = pairing_file.to_string();
        let mac_address_owned = mac_address.to_string();
        RUNTIME.spawn(async move {
            let pairing_file = match PairingFile::read_from_file(&pairing_path) {
                Ok(pf) => pf,
                Err(e) => {
                    let qt_thread = qt_t.clone();
                    qt_thread
                        .queue(move |core_qobj| {
                            core_qobj.no_pairing_file(&QString::from(mac_address_owned));
                        })
                        .ok();
                    eprintln!("Failed to read pairing file: {e}");
                    return;
                }
            };

            let addr = match ip_owned.parse::<IpAddr>() {  
                Ok(addr) => addr,  
                Err(e) => {  
                    //FIXME: emit event for failure
                    eprintln!("Invalid IP address {}: {}", ip_owned, e);  
                    return;  
                }  
            };  

            let t = TcpProvider {  
                addr,  
                pairing_file,  
                label: APP_LABEL.to_string(),
                scope_id : None  
            };


            let result = tokio::select! {
                res = init_idescriptor_device(t, qt_t.clone()) => res,
                /* timeout */ 
                _ = tokio::time::sleep(tokio::time::Duration::from_secs(20)) => {
                    eprintln!("Timeout collecting device info for wireless device mac address: {mac_address_owned}");
                    Err(IdeviceError::Timeout)
                }
            };

            match result {
                Ok((udid, info)) => {
                    // emit event with info
                    let qt_thread = qt_t.clone();

                    qt_thread
                        .queue(move |core_qobj| {
                            core_qobj.device_event(
                                EV_CONNECTED,
                                &QString::from(udid),
                                &QString::from(info),
                            );
                        })
                        .ok();
                }
                Err(e) => {
                    eprintln!("Failed to initialize wireless device mac address: {mac_address_owned} {e:?}");
                    let qt_thread = qt_t.clone();

                    qt_thread
                        .queue(move |core_qobj| {
                            core_qobj.init_failed(&QString::from(mac_address_owned));
                        })
                        .ok();
                }
            }
        });
    }
    fn get_pairing_files(self: Pin<&mut Self>) -> QMap<QMapPair_QString_QVariant> {
        let mut map = QMap::<QMapPair_QString_QVariant>::default();

        #[cfg(not(target_os = "macos"))]
        {
            let paths = match std::fs::read_dir(utils::get_lockdown_path()) {
                Ok(iter) => iter
                    .filter_map(|entry| {
                        let entry = entry.ok()?;
                        let path = entry.path();
                        if path.is_file() { Some(path) } else { None }
                    })
                    .collect::<Vec<_>>(),
                Err(_) => Vec::new(),
            };

            paths.into_iter().for_each(|path| {
                if let Ok(pf) = PairingFile::read_from_file(&path) {
                    let abs = path.canonicalize().unwrap_or(path);
                    let abs_str = abs.to_string_lossy().to_string();

                    map.insert(
                        QString::from(pf.wifi_mac_address),
                        QVariant::from(&QString::from(abs_str)),
                    );
                }
            });
        }

        #[cfg(target_os = "macos")]
        {
            let entries: Vec<(String, String)> = run_sync(async {
                let mut out = Vec::new();

                if let Ok(mut uc) = UsbmuxdConnection::default().await {
                    if let Ok(devs) = uc.get_devices().await {
                        for dev in devs {
                            if let Ok(pair_rec) = uc.get_pair_record(&dev.udid).await {
                                out.push((
                                    pair_rec.wifi_mac_address.clone(),
                                    format!("{}.plist", dev.udid),
                                ));
                            }
                        }
                    }
                }

                out
            });

            let base = utils::get_lockdown_path();

            // turn $UDID.plist into /var/db/lockdown/UDID.plist
            for (wifi_mac, file_name) in entries {
                let full_path = base.join(&file_name);
                let full_path_str = full_path.to_string_lossy().into_owned();

                map.insert(
                    QString::from(wifi_mac),
                    QVariant::from(&QString::from(full_path_str)),
                );
            }
        }

        map
    }
    fn remove_device(self: Pin<&mut Self>, udid: &QString) {
        let udid_str = udid.to_string();
        RUNTIME.spawn(async move {
            clean_device_from_app_state(&udid_str).await;
        });
    }
}

async fn clean_device_from_app_state(udid: &str) {
    let mut state = APP_DEVICE_STATE.lock().await;
    if let Some(svc) = state.remove(udid) {
        if let Some(t) = &svc.heartbeat_task {
            t.abort();
        }

        let mut streams = svc.video_streams.lock().await;
        for (_url, tx) in streams.drain() {
            let _ = tx.send(());
        }
        println!("Removed device with UDID {}", udid);
    } else {
        eprintln!("Attempted to remove non-existent device with UDID {}", udid);
    }
}

fn is_pairing_related_error(e: &IdeviceError) -> bool {
    matches!(
        e,
        IdeviceError::InvalidHostID
            | IdeviceError::PairingDialogResponsePending
            | IdeviceError::PasswordProtected
            | IdeviceError::UserDeniedPairing
            | IdeviceError::CanceledByUser
    )
}

async fn handle_pairing(
    qt_thread: cxx_qt::CxxQtThread<Core>,
    udid: String,
) -> Result<(), IdeviceError> {
    let udid_for_event = udid.clone();
    qt_thread
        .queue(move |core_qobj| {
            core_qobj.device_event(
                EV_PAIRING_PENDING,
                &QString::from(udid_for_event),
                &QString::from(""),
            );
        })
        .ok();

    let mut uc2 = UsbmuxdConnection::default().await?;

    let dev = uc2.get_device(&udid).await?;

    let provider = dev.to_provider(UsbmuxdAddr::default(), APP_LABEL);

    let mut lc = LockdownClient::connect(&provider).await?;

    let buid = uc2.get_buid().await?;

    let host_id = uuid::Uuid::new_v4().to_string().to_uppercase();

    println!(
        "Pairing with device {}, host_id: {}, buid: {}",
        udid, host_id, buid
    );
    let mut pf = loop {
        match lc.pair(host_id.clone(), buid.clone(), None).await {
            Ok(p) => {
                println!(
                    "Pairing successful with device {}, host_id: {}, buid: {}",
                    udid, host_id, buid
                );
                break p;
            }
            Err(IdeviceError::PairingDialogResponsePending) => {
                /* wait */
                tokio::time::sleep(std::time::Duration::from_secs(1)).await;
                continue;
            }
            Err(IdeviceError::PasswordProtected) => {
                /* wait */
                tokio::time::sleep(std::time::Duration::from_secs(1)).await;
                continue;
            }
            Err(IdeviceError::InvalidHostID) => {
                /* wait */
                tokio::time::sleep(std::time::Duration::from_secs(1)).await;
                continue;
            }
            // TODO: we can also check for CanceledByUser or UserDeniedPairing
            Err(e) => {
                return Err(e);
            }
        }
    };
    println!("Paired with device {}, pairing file obtained", udid);
    pf.udid = Some(udid.clone());

    let bytes = pf.serialize()?;
    uc2.save_pair_record(&udid, bytes).await?;

    println!("Pairing record saved to usbmuxd for device {}.", udid);
    Ok(())
}

fn emit_pairing_failed(
    qt_thread: cxx_qt::CxxQtThread<Core>,
    udid: String,
    // FIXME: we may want to use this in the future
    _reason: &str,
) {
    qt_thread
        .queue(move |core_qobj| {
            core_qobj.device_event(EV_FAIL, &QString::from(udid), &QString::from(""));
        })
        .ok();
}

async fn emit_connected(qt_thread: cxx_qt::CxxQtThread<Core>, udid: String) {
    // one init retry after successful pairing
    let mut retried_after_pair = false;

    loop {
        let mut uc = match UsbmuxdConnection::default().await {
            Ok(u) => u,
            Err(_) => return,
        };

        let dev = match uc.get_device(&udid).await {
            Ok(d) => d,
            Err(_) => return,
        };

        let provider = dev.to_provider(UsbmuxdAddr::default(), APP_LABEL);

        match init_idescriptor_device(provider, qt_thread.clone()).await {
            Ok((udid_for_event, info_for_event)) => {
                qt_thread
                    .queue(move |core_qobj| {
                        core_qobj.device_event(
                            EV_CONNECTED,
                            &QString::from(udid_for_event),
                            &QString::from(info_for_event),
                        );
                    })
                    .ok();
                return;
            }
            Err(e) if is_pairing_related_error(&e) && !retried_after_pair => {
                match handle_pairing(qt_thread.clone(), udid.clone()).await {
                    Ok(_) => {
                        retried_after_pair = true;
                        println!(
                            "Pairing succeeded for device {}, retrying initialization.",
                            udid
                        );
                        continue; // retry init once
                    }
                    Err(e) => {
                        eprintln!("Pairing failed for device {}: {e:?}", udid);
                        emit_pairing_failed(
                            qt_thread.clone(),
                            udid.clone(),
                            "Failed to pair device",
                        );
                        return;
                    }
                }
            }
            Err(e) => {
                eprintln!(
                    "Unhandled error while initializing device for udid {}: {e:?}",
                    udid
                );
                return;
            }
        }
    }
}

async fn init_idescriptor_device<
    T: idevice::provider::IdeviceProvider + Send + Sync + Clone + 'static,
>(
    provider: T,
    qt_thread: cxx_qt::CxxQtThread<Core>,
) -> Result<(String, String), IdeviceError> {
    let provider_name = type_name::<T>();
    let is_wireless = provider_name == "idevice::provider::TcpProvider";

    let pf = idevice::provider::IdeviceProvider::get_pairing_file(&provider).await?;
    let mut lc = LockdownClient::connect(&provider).await?;
    lc.start_session(&pf).await?;

    eprintln!("init_idescriptor_device: Attempting to get default values from Lockdown.");
    let mut def_vals = match lc.get_value(None, None).await {
        Ok(v) => v,
        Err(e) => {
            eprintln!("get_value(None, None) failed : {e:?}");
            return Err(e);
        }
    };
    eprintln!("init_idescriptor_device: Default values obtained.");

    // FIXME: we may need our own error types here
    // but InternalError should be fine for now
    let udid = def_vals
        .as_dictionary()
        .ok_or_else(|| {
            IdeviceError::InternalError("Lockdown root is not a dictionary".to_string())
        })?
        .get("UniqueDeviceID")
        .and_then(|v| v.as_string())
        .ok_or_else(|| {
            IdeviceError::InternalError("Missing UniqueDeviceID in Lockdown response".to_string())
        })?
        .to_string();

    if udid.is_empty() {
        eprintln!("init_idescriptor_device: UDID is empty.");
        return Err(IdeviceError::InvalidHostID);
    }
    let mut hb = None;

    if is_wireless {
        eprintln!("init_idescriptor_device: Attempting to connect to HeartbeatClient.");
        hb = match heartbeat::HeartbeatClient::connect(&provider).await {
            Ok(h) => Some(h),
            Err(e) => {
                eprintln!("heartbeat: connect failed: {e:?}");
                return Err(e);
            }
        };
        eprintln!("init_idescriptor_device: Connected to HeartbeatClient.");
    }

    let disk_vals = lc.get_value(None, Some("com.apple.disk_usage")).await?;

    eprintln!("init_idescriptor_device: Attempting to connect to AFC client.");
    let mut afc_client = AfcClient::connect(&provider).await?;

    eprintln!("init_idescriptor_device: Connected to AfcClient.");

    eprintln!("init_idescriptor_device: Attempting to connect to DiagnosticsRelayClient.");
    let mut diag_relay = DiagnosticsRelayClient::connect(&provider).await?;

    eprintln!("init_idescriptor_device: Connected to DiagnosticsRelayClient.");

    let afc2 = match AfcClient::new_afc2(&provider).await {
        Ok(c) => Some(Arc::new(Mutex::new(c))),
        Err(e) => {
            eprintln!("AfcClient::new_afc2 failed: {e:?}");
            None
        }
    };

    eprintln!("init_idescriptor_device: Attempting to get AFC device info.");
    let afc_info = match afc_client.get_device_info().await {
        Ok(i) => i,
        Err(e) => {
            eprintln!("get_device_info failed: {e:?}");
            return Err(e);
        }
    };
    eprintln!("init_idescriptor_device: AFC device info obtained.");

    if let (Value::Dictionary(d_target), Value::Dictionary(d_source)) = (&mut def_vals, disk_vals) {
        d_target.extend(d_source);

        let mut afc_info_dict = plist::Dictionary::new();
        afc_info_dict.insert("Model".into(), Value::String(afc_info.model));
        afc_info_dict.insert(
            "TotalBytes".into(),
            Value::Integer((afc_info.total_bytes as u64).into()),
        );
        afc_info_dict.insert(
            "FreeBytes".into(),
            Value::Integer((afc_info.free_bytes as u64).into()),
        );
        afc_info_dict.insert(
            "BlockSize".into(),
            Value::Integer((afc_info.block_size as u64).into()),
        );

        d_target.insert("AFC_INFO".into(), Value::Dictionary(afc_info_dict));
        d_target.insert(
            "Jailbroken".into(),
            Value::Boolean(utils::detect_jailbroken(&mut afc_client).await),
        );

        if let Some(battery_info) = utils::get_battery_info(&mut diag_relay).await {
            d_target.insert("DIAG_INFO".into(), Value::Dictionary(battery_info));
        }

        d_target.insert(
            "ConnectionType".into(),
            Value::String(if is_wireless {
                "Wireless".into()
            } else {
                "USB".into()
            }),
        );
    }

    eprintln!("init_idescriptor_device: Storing device services.");
    let device_services = DeviceServices {
        afc: Arc::new(Mutex::new(afc_client)),
        afc2,
        diag: Arc::new(Mutex::new(diag_relay)),
        heartbeat_task: None,
        video_streams: Arc::new(Mutex::new(HashMap::new())),
        provider: Arc::new(Mutex::new(Box::new(provider))),
        lockdown: Arc::new(Mutex::new(lc)),
    };

    {
        let mut state = APP_DEVICE_STATE.lock().await;
        if let Some(mut old) = state.insert(udid.to_string(), device_services) {
            eprintln!("device became wired - UDID {}", udid);
            if let Some(task) = old.heartbeat_task.take() {
                task.abort();
            }
            let udid_for_signal = udid.clone();
            qt_thread
                .queue(move |core_qobj| {
                    core_qobj.device_became_wired(&QString::from(udid_for_signal));
                })
                .ok();
        }
    }

    if is_wireless {
        match hb {
            Some(hb_client) => {
                eprintln!("init_idescriptor_device: Spawning heartbeat task.");
                match spawn_heartbeat_task(hb_client, qt_thread.clone(), udid.clone()).await {
                    Ok(task) => {
                        let mut state = APP_DEVICE_STATE.lock().await;
                        if let Some(svc) = state.get_mut(&udid) {
                            svc.heartbeat_task = Some(task);
                        }
                    }
                    Err(()) => {
                        eprintln!("init_idescriptor_device: Failed to spawn heartbeat task.");
                        return Err(IdeviceError::Heartbeat(idevice::HeartbeatError::Unknown));
                    }
                }
            }
            None => {
                eprintln!(
                    "init_idescriptor_device: Heartbeat client is None, cannot spawn heartbeat task."
                );
                return Err(IdeviceError::Heartbeat(idevice::HeartbeatError::Unknown));
            }
        }
    }

    let mut buf = Vec::new();
    if def_vals.to_writer_xml(&mut buf).is_err() {
        eprintln!("init_idescriptor_device: Failed to serialize default values to XML.");
        return Err(IdeviceError::InternalError(
            "Failed to serialize default values to XML".to_string(),
        ));
    }

    let info = String::from_utf8(buf).map_err(|_| {
        IdeviceError::InternalError("Failed to convert default values XML to UTF-8".to_string())
    })?;

    eprintln!("init_idescriptor_device: Device has been initialized.");

    Ok((udid, info))
}

async fn spawn_heartbeat_task(
    mut hb_client: heartbeat::HeartbeatClient,
    qt_thread: cxx_qt::CxxQtThread<Core>,
    udid: String,
) -> Result<Arc<JoinHandle<()>>, ()> {
    let udid_for_hb = udid.clone();
    Ok(Arc::new(RUNTIME.spawn(async move {
        eprintln!("heartbeat: starting heartbeat task ");
        let mut interval = 15u64;
        let mut fails = 0;
        loop {
            eprintln!("heartbeat:  Getting marco (interval: {interval}s)");
            match hb_client.get_marco(interval).await {
                Ok(next) => {
                    interval = next;
                    fails = 0;
                    eprintln!("heartbeat:  Received marco, new interval: {interval}s");
                }
                Err(e) => {
                    fails += 1;
                    eprintln!("heartbeat:  get_marco failed (fail count: {fails}): {e:?}");

                    match e {
                        IdeviceError::Heartbeat(idevice::HeartbeatError::SleepyTime) => {
                            println!("heartbeat: Sleepy time");
                            qt_thread
                                .queue(move |core_qobj| {
                                    core_qobj.sleepy_time_detected();
                                })
                                .ok();
                        }
                        _ => {}
                    };

                    if fails >= 3 {
                        eprintln!("heartbeat: too many failures for  giving up");
                        clean_device_from_app_state(&udid_for_hb).await;

                        let udid_for_event = udid_for_hb.clone();
                        let _ = qt_thread.queue(move |core_qobj| {
                            core_qobj.device_event(
                                EV_DISCONNECTED,
                                &QString::from(udid_for_event),
                                &QString::from(""),
                            );
                        });
                        break;
                    }
                    continue;
                }
            }

            eprintln!("heartbeat:  Sending polo.");
            if let Err(e) = hb_client.send_polo().await {
                fails += 1;
                eprintln!("heartbeat:  send_polo failed (fail count: {fails}): {e:?}");
                match e {
                    IdeviceError::Heartbeat(idevice::HeartbeatError::SleepyTime) => {
                        println!("heartbeat: Sleepy time");
                        qt_thread
                            .queue(move |core_qobj| {
                                core_qobj.sleepy_time_detected();
                            })
                            .ok();
                    }
                    _ => {}
                };
                if fails >= 3 {
                    eprintln!("heartbeat: too many failures for , giving up");
                    clean_device_from_app_state(&udid_for_hb).await;

                    let udid_for_event = udid_for_hb.clone();
                    let _ = qt_thread.queue(move |core_qobj| {
                        core_qobj.device_event(
                            EV_DISCONNECTED,
                            &QString::from(udid_for_event),
                            &QString::from(""),
                        );
                    });
                    break;
                }

                continue;
            }
            eprintln!("heartbeat:  Polo sent successfully.");
            interval += 5;
        }

        eprintln!("heartbeat: heartbeat task ended.");
    })))
}
