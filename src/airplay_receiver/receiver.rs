// SPDX-FileCopyrightText: 2026 Uncore <https://github.com/uncor3>
// SPDX-License-Identifier: GPL-3.0-or-later

//! Receiver lifecycle which coordinates the protocol, playback, and discovery.

use std::{collections::BTreeMap, sync::Arc};

use anyhow::{Context, Result, anyhow, bail};
use log::{debug, info};
use rsplay::{
    AirPlayMode, AudioHandler, DisplayConfig, PairingStore, RaopServer, ReceiverMode, VideoHandler,
};
use tokio::sync::mpsc;
use tokio_util::sync::CancellationToken;

use super::{
    discovery::{
        DiscoveryBackend, DiscoveryEvent, ServiceAdvertisement, ServiceKind, ZeroconfDiscovery,
    },
    playback::GstreamerPlayback,
};

/// Runtime configuration for an rsplay receiver.
#[derive(Clone, Debug)]
pub struct ReceiverConfig {
    pub name: String,
    pub device_id: [u8; 6],
    /// RTSP port. Zero requests an operating-system assigned port.
    pub port: u16,
    pub max_clients: usize,
    pub use_legacy_ports: bool,
    pub display_width: u16,
    pub display_height: u16,
    pub display_refresh_rate: u8,
    pub max_fps: u8,
    pub overscanned: bool,
    pub h265: bool,
    pub audio_only: bool,
}

impl ReceiverConfig {
    pub fn validate(&self) -> Result<()> {
        if self.name.trim().is_empty() {
            bail!("the AirPlay receiver name must not be empty");
        }
        if self.max_clients == 0 {
            bail!("the AirPlay maximum client count must be greater than zero");
        }
        Ok(())
    }
}

/// Application-facing receiver lifecycle notifications.
#[derive(Clone, Debug, Eq, PartialEq)]
pub enum ReceiverEvent {
    Ready {
        port: u16,
    },
    ClientConnected {
        address: String,
    },
    ClientDetails {
        device_id: String,
        model: String,
        name: String,
    },
    ClientDisconnected,
    AudioActivityChanged {
        active: bool,
    },
    Error(String),
    Stopped,
}

/// Complete AirPlay receiver assembled from Rust protocol and playback components.
pub struct Receiver {
    config: ReceiverConfig,
    playback: Arc<GstreamerPlayback>,
    pairing_store: Arc<dyn PairingStore>,
    events: mpsc::UnboundedSender<ReceiverEvent>,
}

impl Receiver {
    pub fn new(
        config: ReceiverConfig,
        playback: Arc<GstreamerPlayback>,
        pairing_store: Arc<dyn PairingStore>,
        events: mpsc::UnboundedSender<ReceiverEvent>,
    ) -> Result<Self> {
        config.validate()?;
        Ok(Self {
            config,
            playback,
            pairing_store,
            events,
        })
    }

    /// Run until cancellation or a fatal protocol/discovery error.
    pub async fn run(self, cancellation: CancellationToken) -> Result<()> {
        let audio_handler: Arc<dyn AudioHandler> = self.playback.clone();
        let video_handler: Arc<dyn VideoHandler> = self.playback;
        let mut server = RaopServer::builder()
            .name(&self.config.name)
            .hwaddr(self.config.device_id)
            .port(self.config.port)
            .max_clients(self.config.max_clients)
            .use_legacy_ports(self.config.use_legacy_ports)
            .display(DisplayConfig {
                width: self.config.display_width,
                height: self.config.display_height,
                refresh_rate: self.config.display_refresh_rate,
                max_fps: self.config.max_fps,
                overscanned: self.config.overscanned,
            })
            .h265(self.config.h265)
            .receiver_mode(if self.config.audio_only {
                ReceiverMode::AudioOnly
            } else {
                ReceiverMode::Mirroring
            })
            // Match UxPlay's legacy mirroring profile. Advertising the AirPlay 2
            // profile makes iOS initiate HomeKit TLV/SRP pairing instead.
            .mode(AirPlayMode::Mirroring)
            .pairing_store(self.pairing_store)
            .video_handler(video_handler)
            .build(audio_handler)
            .context("failed to construct the AirPlay protocol server")?;

        server
            .start_without_discovery()
            .await
            .context("failed to start the AirPlay protocol listener")?;
        let port = server.port();
        info!("rsplay protocol listener started on port {port}");

        let discovery = ZeroconfDiscovery::from_advertisements(advertisements(&server))?;
        let (discovery_sender, mut discovery_events) = mpsc::unbounded_channel();
        let discovery_cancellation = cancellation.child_token();
        let discovery_run_cancellation = discovery_cancellation.clone();
        let mut discovery_task =
            tokio::spawn(Box::new(discovery).run(discovery_run_cancellation, discovery_sender));

        let mut registered_airplay = false;
        let mut registered_raop = false;
        let mut fatal_error = None;

        loop {
            tokio::select! {
                _ = cancellation.cancelled() => break,
                event = discovery_events.recv() => match event {
                    Some(DiscoveryEvent::Registered { kind, .. }) => {
                        match kind {
                            ServiceKind::Airplay => registered_airplay = true,
                            ServiceKind::Raop => registered_raop = true,
                        }
                        if registered_airplay && registered_raop {
                            let _ = self.events.send(ReceiverEvent::Ready { port });
                        }
                    }
                    Some(DiscoveryEvent::RegistrationFailed { kind, message }) => {
                        fatal_error = Some(anyhow!("{kind:?} discovery failed: {message}"));
                        break;
                    }
                    Some(DiscoveryEvent::Stopped { kind }) => {
                        debug!("rsplay {kind:?} discovery stopped");
                    }
                    None => {
                        fatal_error = Some(anyhow!("the rsplay discovery event channel closed"));
                        break;
                    }
                },
                result = &mut discovery_task => {
                    match result {
                        Ok(Ok(())) if cancellation.is_cancelled() => {},
                        Ok(Ok(())) => fatal_error = Some(anyhow!("rsplay discovery stopped unexpectedly")),
                        Ok(Err(err)) => fatal_error = Some(err.context("rsplay discovery failed")),
                        Err(err) => fatal_error = Some(anyhow!("the rsplay discovery task failed: {err}")),
                    }
                    break;
                }
            }
        }

        discovery_cancellation.cancel();
        server.stop().await;
        if !discovery_task.is_finished() {
            discovery_task
                .await
                .context("the rsplay discovery task failed during shutdown")??;
        }
        let _ = self.events.send(ReceiverEvent::Stopped);

        if let Some(err) = fatal_error {
            let _ = self.events.send(ReceiverEvent::Error(err.to_string()));
            return Err(err);
        }
        Ok(())
    }
}

fn advertisements(server: &RaopServer) -> [ServiceAdvertisement; 2] {
    let info = server.service_info();
    let airplay_txt = info.airplay_txt.into_iter().collect::<BTreeMap<_, _>>();
    let raop_txt = info.raop_txt.into_iter().collect::<BTreeMap<_, _>>();
    [
        ServiceAdvertisement {
            kind: ServiceKind::Airplay,
            name: info.airplay_name,
            port: info.port,
            txt: airplay_txt,
        },
        ServiceAdvertisement {
            kind: ServiceKind::Raop,
            name: info.raop_name,
            port: info.port,
            txt: raop_txt,
        },
    ]
}
