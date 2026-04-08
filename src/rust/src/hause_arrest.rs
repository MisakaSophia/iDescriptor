use crate::{APP_DEVICE_STATE, RUNTIME, afc, run_sync, utils};
use cxx_qt::{CxxQtType, Threading};
use cxx_qt_lib::{QByteArray, QMap, QMapPair_QString_QVariant, QString};
use idevice::afc::{AfcClient, opcode::AfcFopenMode};
use std::pin::Pin;
use std::sync::Arc;

use tokio::{
    io::AsyncReadExt,
    net::TcpListener,
    sync::{Mutex, oneshot},
};

#[cxx_qt::bridge(namespace = "CXX")]
mod qobject {
    #[namespace = ""]
    unsafe extern "C++" {
        include!("cxx-qt-lib/qstring.h");
        include!("cxx-qt-lib/qlist.h");
        include!("cxx-qt-lib/qbytearray.h");
        include!("cxx-qt-lib/qmap.h");
        include!("cxx-qt-lib/qvariant.h");

        type QString = cxx_qt_lib::QString;
        type QMap_QString_QVariant = cxx_qt_lib::QMap<cxx_qt_lib::QMapPair_QString_QVariant>;
        type QByteArray = cxx_qt_lib::QByteArray;

    }

    extern "RustQt" {
        #[qobject]
        type HauseArrest = super::RHauseArrestBackend;

        #[qinvokable]
        fn get_bundle_id(self: &HauseArrest) -> &QString;

        #[qinvokable]
        fn init_session(self: Pin<&mut HauseArrest>);
        #[qsignal]
        fn init_session_finished(self: Pin<&mut HauseArrest>, success: bool);

        #[qinvokable]
        fn check_is_dir_and_list(self: &HauseArrest, path: &QString);
        #[qsignal]
        fn check_is_dir_and_list_finished(
            self: Pin<&mut HauseArrest>,
            success: bool,
            entries: &QMap_QString_QVariant,
        );

        #[qinvokable]
        fn file_to_buffer(self: &HauseArrest, file_path: &QString) -> QByteArray;

        #[qinvokable]
        fn start_video_stream(self: &HauseArrest, file_path: &QString) -> QString;

        #[qinvokable]
        fn delete_path(self: &HauseArrest, path: &QString) -> bool;
    }

    impl cxx_qt::Threading for HauseArrest {}

    impl cxx_qt::Constructor<(QString, QString), NewArguments = (QString, QString)> for HauseArrest {}
}

#[derive(Default)]
pub struct RHauseArrestBackend {
    udid: QString,
    bundle_id: QString,
    afc_handle: Option<Arc<Mutex<AfcClient>>>,
}

impl cxx_qt::Constructor<(QString, QString)> for qobject::HauseArrest {
    type BaseArguments = ();
    type InitializeArguments = ();
    type NewArguments = (QString, QString);

    fn route_arguments(
        args: (QString, QString),
    ) -> (
        Self::NewArguments,
        Self::BaseArguments,
        Self::InitializeArguments,
    ) {
        (args, (), ())
    }

    fn new(args: (QString, QString)) -> RHauseArrestBackend {
        RHauseArrestBackend {
            udid: args.0,
            bundle_id: args.1,
            afc_handle: None,
        }
    }
}

impl qobject::HauseArrest {
    fn get_bundle_id(&self) -> &QString {
        use cxx_qt::CxxQtType;
        &self.rust().bundle_id
    }

    fn init_session(self: Pin<&mut Self>) {
        let udid_str = self.udid.to_string();
        let bundle_id_str = self.bundle_id.to_string();
        let qt_thread = self.qt_thread();

        RUNTIME.spawn(async move {
            let afc_client = {
                let maybe_device = APP_DEVICE_STATE.lock().await.get(&udid_str).cloned();
                let device = match maybe_device {
                    Some(d) => d,
                    None => {
                        eprintln!("HouseArrest: Device {} not found", udid_str);

                        qt_thread
                            .queue(move |q| {
                                q.init_session_finished(false);
                            })
                            .ok();
                        return;
                    }
                };

                let provider_guard = device.provider.lock().await;

                match utils::vend_app_documents(provider_guard.as_ref(), &bundle_id_str).await {
                    Ok(afc_client) => afc_client,
                    Err(e) => {
                        eprintln!(
                            "Failed to initialize HouseArrest session for {}: {}",
                            bundle_id_str, e
                        );
                        qt_thread
                            .queue(move |q| {
                                q.init_session_finished(false);
                            })
                            .ok();
                        return;
                    }
                }
            };

            qt_thread
                .queue(move |mut qobject| {
                    qobject.as_mut().rust_mut().afc_handle = Some(Arc::new(Mutex::new(afc_client)));
                })
                .ok();

            qt_thread
                .queue(move |q| {
                    q.init_session_finished(true);
                })
                .ok();
        });
    }

    fn check_is_dir_and_list(self: &Self, path: &QString) {
        let qt_t = self.qt_thread();
        let path_str = path.to_string();

        let afc_opt = self.rust().afc_handle.clone();

        RUNTIME.spawn(async move {
            let qt_thread = qt_t.clone();

            let Some(afc_handle) = afc_opt else {
                eprintln!("HouseArrest: AfcClient not initialized");
                qt_thread
                    .queue(move |q| {
                        q.check_is_dir_and_list_finished(
                            false,
                            &QMap::<QMapPair_QString_QVariant>::default(),
                        );
                    })
                    .ok();
                return;
            };

            let mut afc_client = afc_handle.lock().await;
            let map_result = afc::check_is_dir_and_list(&mut *afc_client, path_str).await;

            qt_thread
                .queue(move |q| {
                    q.check_is_dir_and_list_finished(true, &map_result);
                })
                .ok();
        });
    }

    fn file_to_buffer(&self, album_path: &QString) -> QByteArray {
        let album_path_string = album_path.to_string();
        let afc_opt = self.rust().afc_handle.clone();

        let data: Vec<u8> = run_sync(async move {
            let Some(afc_handle) = afc_opt else {
                eprintln!("HouseArrest: AfcClient not initialized");
                return Vec::new();
            };

            let mut afc_client = afc_handle.lock().await;

            let mut fd = match afc_client
                .open(album_path_string.clone(), AfcFopenMode::RdOnly)
                .await
            {
                Ok(f) => f,
                Err(e) => {
                    eprintln!("file_to_buffer: failed to open {album_path_string}: {e}");
                    return Vec::new();
                }
            };

            let mut buf = Vec::new();
            let mut chunk = vec![0u8; 8192];

            loop {
                let n = match fd.read(&mut chunk).await {
                    Ok(n) => n,
                    Err(e) => {
                        eprintln!("file_to_buffer: failed to read {album_path_string}: {e}");
                        buf.clear();
                        break;
                    }
                };
                if n == 0 {
                    break;
                }
                buf.extend_from_slice(&chunk[..n]);
            }
            fd.close().await.ok();
            buf
        });

        if data.is_empty() {
            QByteArray::default()
        } else {
            QByteArray::from(&data[..])
        }
    }

    fn start_video_stream(&self, file_path: &QString) -> QString {
        let afc_opt = self.rust().afc_handle.clone();

        let Some(afc) = afc_opt else {
            eprintln!("HouseArrest: AfcClient not initialized");
            return QString::default();
        };

        let udid_str = self.udid.to_string();
        let path_str = file_path.to_string();
        let cloned_path = path_str.clone();

        eprintln!(
            "start_video_stream: request udid={} path={}",
            udid_str, cloned_path
        );

        // bind ephemeral port on localhost
        let listener = match std::net::TcpListener::bind("127.0.0.1:0") {
            Ok(l) => l,
            Err(e) => {
                eprintln!("start_video_stream: bind failed: {e}");
                return QString::default();
            }
        };
        let local_addr = match listener.local_addr() {
            Ok(a) => a,
            Err(e) => {
                eprintln!("start_video_stream: local_addr failed: {e}");
                return QString::default();
            }
        };
        listener.set_nonblocking(true).ok();

        // create Tokio TcpListener inside runtime
        let std_listener = {
            let _guard = RUNTIME.handle().enter();
            match TcpListener::from_std(listener) {
                Ok(l) => l,
                Err(e) => {
                    eprintln!("start_video_stream: from_std failed: {e}");
                    return QString::default();
                }
            }
        };

        let port = local_addr.port();

        let encoded = urlencoding::encode(&cloned_path);
        let url = format!("http://127.0.0.1:{}/{}", port, encoded);
        let url_clone = url.clone();
        let url_clone_for_log = url.clone();
        let (shutdown_tx, mut shutdown_rx) = oneshot::channel::<()>();
        let udid_for_insert = udid_str.clone();
        let url_for_insert = url.clone();
        let inserted = run_sync(async move {
            let maybe_device = APP_DEVICE_STATE.lock().await.get(&udid_for_insert).cloned();
            let device = match maybe_device {
                Some(d) => d,
                None => return false,
            };

            let mut video_streams = device.video_streams.lock().await;
            video_streams.insert(url_for_insert, shutdown_tx);
            true
        });
        if !inserted {
            eprintln!(
                "start_video_stream: failed to insert video stream for udid={} path={}",
                udid_str, cloned_path
            );
            return QString::default();
        }
        eprintln!(
            "start_video_stream: serving {} for udid={} path={}",
            url_clone, udid_str, cloned_path
        );
        // accept-loop task
        RUNTIME.spawn(async move {
            loop {
                tokio::select! {
                    _ = &mut shutdown_rx => {
                        // shutdown requested
                        eprintln!("start_video_stream: shutdown requested for {}", url_clone);
                        break;
                    }
                    accept_res = std_listener.accept() => {
                        let (socket, peer) = match accept_res {
                            Ok(s) => s,
                            Err(e) => {
                                eprintln!("start_video_stream: accept error: {e} on {}", url_clone);
                                break;
                            }
                        };
                        eprintln!("start_video_stream: accepted connection from {} on {}", peer, url_clone);

                        let path_clone = path_str.clone();
                        let afc_for_conn = afc.clone();
                        
                        tokio::spawn(async move {
                            let mut afc = afc_for_conn.lock().await;
                            afc::handle_http_connection(&mut afc, path_clone, socket).await;
                        });
                    }
                }
            }
            eprintln!("start_video_stream: accept-loop exiting for {}", url_clone);
        });

        QString::from(url_clone_for_log)
    }
    fn delete_path(&self, path: &QString) -> bool {
        let path_str = path.to_string();
        let afc_opt = self.rust().afc_handle.clone();

        run_sync(async move {
            let Some(afc_handle) = afc_opt else {
                eprintln!("HouseArrest: AfcClient not initialized");
                return false;
            };

            let mut afc_client = afc_handle.lock().await;

            match afc_client.remove(&path_str).await {
                Ok(_) => true,
                Err(e) => {
                    eprintln!("delete_path: failed to delete {}: {}", path_str, e);
                    false
                }
            }
        })
    }
}
