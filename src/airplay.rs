// SPDX-FileCopyrightText: 2025-2026 Uncore <https://github.com/uncor3>
// SPDX-License-Identifier: AGPL-3.0-or-later

use crate::{
    RUNTIME,
    airplay_receiver::{
        GstreamerPlayback, PersistentPairingStore, Receiver, ReceiverConfig, ReceiverEvent,
    },
    qt_threading::{QtThread, QtThreading},
};
use log::{debug, error, info};
use macros::QtThreading;
use qmetaobject::prelude::*;
use std::sync::{
    Arc, Mutex,
    atomic::{AtomicU64, Ordering},
};
use tokio::sync::mpsc;
use tokio::task::JoinHandle;
use tokio_util::sync::CancellationToken;

#[allow(non_snake_case)]
#[derive(QObject, Default, QtThreading)]
pub struct Airplay {
    base: qt_base_class!(trait QObject),
    init: qt_method!(fn(&mut self, video_item: QVariant) -> bool),
    start_audio: qt_method!(fn(&mut self) -> bool),
    cleanup: qt_method!(fn(&mut self)),
    load_gst_gl: qt_method!(fn(&self) -> bool),
    set_master_volume: qt_method!(fn(&self, volume: f64)),
    connectionChange: qt_signal!(connected: bool),
    connectionDetailsChanged: qt_signal!(name: QString, model: QString, parsed_model: QString, device_id: QString),
    serverReady: qt_signal!(port: i32),
    backendFailed: qt_signal!(code: i32, detail: QString),
    mode: qt_property!(i32; NOTIFY modeChanged),
    state: qt_property!(i32; NOTIFY stateChanged),
    audioActive: qt_property!(bool; NOTIFY audioActiveChanged),
    clientName: qt_property!(QString; NOTIFY clientChanged),
    lastError: qt_property!(QString; NOTIFY errorChanged),
    receiverPort: qt_property!(i32; NOTIFY receiverPortChanged),
    modeChanged: qt_signal!(),
    stateChanged: qt_signal!(),
    audioActiveChanged: qt_signal!(),
    clientChanged: qt_signal!(),
    errorChanged: qt_signal!(),
    receiverPortChanged: qt_signal!(),
    cancellation: Mutex<Option<CancellationToken>>,
    receiver_task: Mutex<Option<JoinHandle<()>>>,
    playback: Mutex<Option<Arc<GstreamerPlayback>>>,
    generation: Arc<AtomicU64>,
}

impl Airplay {
    const MODE_STOPPED: i32 = 0;
    const MODE_MIRRORING: i32 = 1;
    const MODE_AUDIO_ONLY: i32 = 2;
    const STATE_STOPPED: i32 = 0;
    const STATE_STARTING: i32 = 1;
    const STATE_LISTENING: i32 = 2;
    const STATE_CONNECTED: i32 = 3;
    const STATE_STOPPING: i32 = 4;
    const STATE_FAILED: i32 = 5;

    fn load_gst_gl(&self) -> bool {
        GstreamerPlayback::qml_sink_available()
    }

    fn init(&mut self, video_item: QVariant) -> bool {
        let video_item = crate::utils::qvariant_to_ptr(video_item);
        self.start_receiver(Self::MODE_MIRRORING, video_item)
    }

    fn start_audio(&mut self) -> bool {
        self.start_receiver(Self::MODE_AUDIO_ONLY, 0)
    }

    fn start_receiver(&mut self, requested_mode: i32, video_item: usize) -> bool {
        let previous_task = self.stop_current(false);
        self.mode = requested_mode;
        self.state = Self::STATE_STARTING;
        self.audioActive = false;
        self.lastError = QString::default();
        self.modeChanged();
        self.stateChanged();
        self.audioActiveChanged();
        self.errorChanged();

        let (events, event_receiver) = mpsc::unbounded_channel();
        let playback_result = if requested_mode == Self::MODE_AUDIO_ONLY {
            GstreamerPlayback::new_audio_only(events.clone())
        } else {
            GstreamerPlayback::new(video_item, events.clone())
        };
        let playback = match playback_result {
            Ok(playback) => playback,
            Err(err) => {
                error!("Failed to initialize AirPlay playback: {err:#}");
                self.state = Self::STATE_FAILED;
                self.lastError = QString::from(err.to_string());
                self.stateChanged();
                self.errorChanged();
                self.backendFailed(-1, QString::from(err.to_string()));
                return false;
            }
        };
        let pairing_store = match PersistentPairingStore::open_default() {
            Ok(store) => Arc::new(store),
            Err(err) => {
                error!("Failed to open the AirPlay pairing store: {err:#}");
                self.state = Self::STATE_FAILED;
                self.lastError = QString::from(err.to_string());
                self.stateChanged();
                self.errorChanged();
                self.backendFailed(-1, QString::from(err.to_string()));
                return false;
            }
        };
        let conf = ReceiverConfig {
            name: "iDescriptor".to_owned(),
            device_id: pairing_store.device_id(),
            port: 0,
            max_clients: 1,
            use_legacy_ports:
                crate::settings_manager::SettingsManager::airplay_use_legacy_ports_enabled(),
            display_width: crate::settings_manager::SettingsManager::airplay_width_value(),
            display_height: crate::settings_manager::SettingsManager::airplay_height_value(),
            display_refresh_rate:
                crate::settings_manager::SettingsManager::airplay_refresh_rate_value(),
            max_fps: crate::settings_manager::SettingsManager::airplay_fps_value(),
            overscanned: crate::settings_manager::SettingsManager::airplay_overscanned_enabled(),
            h265: crate::settings_manager::SettingsManager::airplay_h265_enabled(),
            audio_only: requested_mode == Self::MODE_AUDIO_ONLY,
        };

        debug!("Starting receiver with config: {conf:?}");

        let receiver = match Receiver::new(conf, playback.clone(), pairing_store, events) {
            Ok(receiver) => receiver,
            Err(err) => {
                error!("Failed to configure the AirPlay receiver: {err:#}");
                self.state = Self::STATE_FAILED;
                self.lastError = QString::from(err.to_string());
                self.stateChanged();
                self.errorChanged();
                self.backendFailed(-1, QString::from(err.to_string()));
                return false;
            }
        };

        let cancellation = CancellationToken::new();
        let generation = self.generation.fetch_add(1, Ordering::AcqRel) + 1;
        *self
            .cancellation
            .lock()
            .expect("AirPlay cancellation lock poisoned") = Some(cancellation.clone());
        *self
            .playback
            .lock()
            .expect("AirPlay playback lock poisoned") = Some(playback);
        let q_thread = self.qt_thread();
        let event_generation = self.generation.clone();
        RUNTIME.spawn(forward_events(
            event_receiver,
            q_thread.clone(),
            event_generation,
            generation,
        ));

        let run_generation = self.generation.clone();
        let receiver_task = RUNTIME.spawn(async move {
            if let Some(previous_task) = previous_task {
                let _ = previous_task.await;
            }
            info!("Starting AirPlay receiver");
            if let Err(err) = receiver.run(cancellation).await {
                error!("AirPlay receiver stopped with an error: {err:#}");
                if run_generation.load(Ordering::Acquire) == generation {
                    q_thread.queue(move |t| {
                        if run_generation.load(Ordering::Acquire) == generation {
                            t.state = Self::STATE_FAILED;
                            t.lastError = QString::from(err.to_string());
                            t.stateChanged();
                            t.errorChanged();
                            t.backendFailed(-1, QString::from(err.to_string()));
                        }
                    });
                }
            }
        });
        *self
            .receiver_task
            .lock()
            .expect("AirPlay receiver task lock poisoned") = Some(receiver_task);
        true
    }

    fn stop_current(&mut self, update_state: bool) -> Option<JoinHandle<()>> {
        self.generation.fetch_add(1, Ordering::AcqRel);
        if let Some(cancellation) = self
            .cancellation
            .lock()
            .expect("AirPlay cancellation lock poisoned")
            .take()
        {
            debug!("Stopping AirPlay receiver");
            cancellation.cancel();
        }
        self.playback
            .lock()
            .expect("AirPlay playback lock poisoned")
            .take();
        let task = self
            .receiver_task
            .lock()
            .expect("AirPlay receiver task lock poisoned")
            .take();
        self.audioActive = false;
        self.audioActiveChanged();
        if update_state {
            self.mode = Self::MODE_STOPPED;
            self.state = Self::STATE_STOPPING;
            self.modeChanged();
            self.stateChanged();
        }
        self.connectionChange(false);
        task
    }

    fn cleanup(&mut self) {
        let previous_task = self.stop_current(true);
        let q_thread = self.qt_thread();
        RUNTIME.spawn(async move {
            if let Some(previous_task) = previous_task {
                let _ = previous_task.await;
            }
            q_thread.queue(|t| {
                if t.mode == Self::MODE_STOPPED {
                    t.state = Self::STATE_STOPPED;
                    t.stateChanged();
                }
            });
        });
    }

    fn set_master_volume(&self, volume: f64) {
        let volume = volume.clamp(0.0, 1.0) as f32;
        debug!("Setting AirPlay master volume to {:.0}%", volume * 100.0);
        if let Some(playback) = self
            .playback
            .lock()
            .expect("AirPlay playback lock poisoned")
            .as_ref()
        {
            playback.set_volume(volume);
        }
    }
}

async fn forward_events(
    mut events: mpsc::UnboundedReceiver<ReceiverEvent>,
    q_thread: QtThread<Airplay>,
    active_generation: Arc<AtomicU64>,
    generation: u64,
) {
    while let Some(event) = events.recv().await {
        if active_generation.load(Ordering::Acquire) != generation {
            return;
        }

        match event {
            ReceiverEvent::Ready { port } => q_thread.queue({
                let active_generation = active_generation.clone();
                move |t| {
                    if active_generation.load(Ordering::Acquire) == generation {
                        t.receiverPort = port as i32;
                        t.state = Airplay::STATE_LISTENING;
                        t.receiverPortChanged();
                        t.stateChanged();
                        t.serverReady(port as i32);
                    }
                }
            }),
            ReceiverEvent::ClientConnected { address } => {
                debug!("AirPlay client connected from {address}");
                q_thread.queue({
                    let active_generation = active_generation.clone();
                    move |t| {
                        if active_generation.load(Ordering::Acquire) == generation {
                            t.state = Airplay::STATE_CONNECTED;
                            t.stateChanged();
                            t.connectionChange(true);
                        }
                    }
                });
            }
            ReceiverEvent::ClientDetails {
                device_id,
                model,
                name,
            } => {
                let parsed_model = crate::device_db::find_by_identifier(&model);
                q_thread.queue({
                    let active_generation = active_generation.clone();
                    move |t| {
                        if active_generation.load(Ordering::Acquire) == generation {
                            t.clientName = QString::from(name.clone());
                            t.clientChanged();
                            t.connectionDetailsChanged(
                                QString::from(name),
                                QString::from(model),
                                QString::from(
                                    parsed_model
                                        .unwrap_or(&crate::device_db::UNKNOWN_DEVICE)
                                        .display_name,
                                ),
                                QString::from(device_id),
                            );
                        }
                    }
                });
            }
            ReceiverEvent::ClientDisconnected => q_thread.queue({
                let active_generation = active_generation.clone();
                move |t| {
                    if active_generation.load(Ordering::Acquire) == generation {
                        t.state = Airplay::STATE_LISTENING;
                        t.clientName = QString::default();
                        t.audioActive = false;
                        t.stateChanged();
                        t.clientChanged();
                        t.audioActiveChanged();
                        t.connectionChange(false);
                    }
                }
            }),
            ReceiverEvent::AudioActivityChanged { active } => q_thread.queue({
                let active_generation = active_generation.clone();
                move |t| {
                    if active_generation.load(Ordering::Acquire) == generation
                        && t.audioActive != active
                    {
                        t.audioActive = active;
                        t.audioActiveChanged();
                    }
                }
            }),
            ReceiverEvent::Error(detail) => q_thread.queue({
                let active_generation = active_generation.clone();
                move |t| {
                    if active_generation.load(Ordering::Acquire) == generation {
                        t.state = Airplay::STATE_FAILED;
                        t.lastError = QString::from(detail.clone());
                        t.stateChanged();
                        t.errorChanged();
                        t.backendFailed(-1, QString::from(detail));
                    }
                }
            }),
            ReceiverEvent::Stopped => debug!("AirPlay receiver stopped"),
        }
    }
}
