// SPDX-FileCopyrightText: 2026 Uncore <https://github.com/uncor3>
// SPDX-License-Identifier: GPL-3.0-or-later

use std::{
    sync::{Arc, Mutex},
    time::Duration,
};

use anyhow::{Context, Result, anyhow};
#[cfg(target_os = "macos")]
use izeroconf::bonjour::{
    service::BonjourMdnsService as BackendMdnsService,
    txt_record::BonjourTxtRecord as BackendTxtRecord,
};
#[cfg(not(target_os = "macos"))]
use izeroconf::pure_rust::{
    service::PureRustMdnsService as BackendMdnsService,
    txt_record::PureRustTxtRecord as BackendTxtRecord,
};
use izeroconf::{NetworkInterface, ServiceType, prelude::*};
use tokio::{sync::mpsc, task::JoinHandle};
use tokio_util::sync::CancellationToken;

use super::{
    DiscoveryBackend, DiscoveryConfig, DiscoveryEvent, DiscoveryFuture, ServiceAdvertisement,
};

const POLL_INTERVAL: Duration = Duration::from_millis(100);

/// DNS-SD backend using izeroconf's pure-Rust mdns-sd implementation.
pub struct ZeroconfDiscovery {
    advertisements: [ServiceAdvertisement; 2],
}

impl ZeroconfDiscovery {
    pub fn new(config: DiscoveryConfig) -> Result<Self> {
        Ok(Self {
            advertisements: config.advertisements()?,
        })
    }

    pub fn advertisements(&self) -> &[ServiceAdvertisement; 2] {
        &self.advertisements
    }

    /// Create a backend from protocol-engine supplied DNS-SD records.
    pub fn from_advertisements(advertisements: [ServiceAdvertisement; 2]) -> Result<Self> {
        for advertisement in &advertisements {
            if advertisement.name.trim().is_empty() {
                return Err(anyhow!("the DNS-SD service name must not be empty"));
            }
            if advertisement.port == 0 {
                return Err(anyhow!("the DNS-SD service port must not be zero"));
            }
        }
        Ok(Self { advertisements })
    }
}

impl DiscoveryBackend for ZeroconfDiscovery {
    fn run(
        self: Box<Self>,
        cancellation: CancellationToken,
        events: mpsc::UnboundedSender<DiscoveryEvent>,
    ) -> DiscoveryFuture {
        // this needs more work for some reason pure rust backend doesn't work
        // on macOS and using the native backend makes more sense
        Box::pin(async move {
            #[cfg(target_os = "macos")]
            let worker =
                spawn_native_registrations(self.advertisements, cancellation.clone(), events);
            #[cfg(not(target_os = "macos"))]
            let worker = spawn_registrations(self.advertisements, cancellation.clone(), events);
            let result = worker
                .await
                .context("the mDNS worker panicked or was cancelled")?;
            cancellation.cancel();
            result.context("the mDNS worker failed")
        })
    }
}

#[cfg(target_os = "macos")]
fn spawn_native_registrations(
    advertisements: [ServiceAdvertisement; 2],
    cancellation: CancellationToken,
    events: mpsc::UnboundedSender<DiscoveryEvent>,
) -> JoinHandle<Result<()>> {
    tokio::spawn(async move {
        let [airplay, raop] = advertisements;
        let airplay_worker =
            spawn_native_registration(airplay, cancellation.clone(), events.clone());
        let raop_worker = spawn_native_registration(raop, cancellation.clone(), events);
        let (airplay_result, raop_result) = tokio::join!(airplay_worker, raop_worker);
        cancellation.cancel();
        flatten_worker_result(airplay_result, "AirPlay")?;
        flatten_worker_result(raop_result, "RAOP")
    })
}

#[cfg(target_os = "macos")]
fn spawn_native_registration(
    advertisement: ServiceAdvertisement,
    cancellation: CancellationToken,
    events: mpsc::UnboundedSender<DiscoveryEvent>,
) -> JoinHandle<Result<()>> {
    tokio::task::spawn_blocking(move || {
        let kind = advertisement.kind;
        let requested_name = advertisement.name.clone();
        let callback_error = Arc::new(Mutex::new(None::<String>));
        let callback_error_writer = callback_error.clone();
        let callback_cancellation = cancellation.clone();
        let callback_events = events.clone();

        let mut service = build_service(&advertisement)?;
        service.set_registered_callback(Box::new(move |result, _context| match result {
            Ok(registration) if registration.name() == &requested_name => {
                let _ = callback_events.send(DiscoveryEvent::Registered {
                    kind,
                    requested_name: requested_name.clone(),
                    registered_name: registration.name().clone(),
                });
            }
            Ok(registration) => {
                let message = format!(
                    "mDNS changed the requested service name from {:?} to {:?}",
                    requested_name,
                    registration.name()
                );
                record_callback_error(&callback_error_writer, &message);
                let _ = callback_events.send(DiscoveryEvent::RegistrationFailed { kind, message });
                callback_cancellation.cancel();
            }
            Err(err) => {
                let message = err.to_string();
                record_callback_error(&callback_error_writer, &message);
                let _ = callback_events.send(DiscoveryEvent::RegistrationFailed { kind, message });
                callback_cancellation.cancel();
            }
        }));

        let event_loop = service.register().map_err(|err| {
            let message = format!("failed to register {}: {err}", advertisement.name);
            let _ = events.send(DiscoveryEvent::RegistrationFailed { kind, message });
            cancellation.cancel();
            anyhow!(err.to_string())
        })?;

        while !cancellation.is_cancelled() {
            if let Err(err) = event_loop.poll(POLL_INTERVAL) {
                let message = format!("mDNS polling failed for {}: {err}", advertisement.name);
                let _ = events.send(DiscoveryEvent::RegistrationFailed {
                    kind,
                    message: message.clone(),
                });
                cancellation.cancel();
                return Err(anyhow!(message));
            }
        }

        drop(event_loop);
        drop(service);
        let _ = events.send(DiscoveryEvent::Stopped { kind });

        if let Some(message) = callback_error
            .lock()
            .map_err(|_| anyhow!("the mDNS callback error lock was poisoned"))?
            .take()
        {
            return Err(anyhow!(message));
        }
        Ok(())
    })
}

#[cfg(target_os = "macos")]
fn flatten_worker_result(
    result: std::result::Result<Result<()>, tokio::task::JoinError>,
    service: &str,
) -> Result<()> {
    result
        .with_context(|| format!("the {service} mDNS worker panicked or was cancelled"))?
        .with_context(|| format!("the {service} mDNS worker failed"))
}

#[cfg(not(target_os = "macos"))]
fn spawn_registrations(
    advertisements: [ServiceAdvertisement; 2],
    cancellation: CancellationToken,
    events: mpsc::UnboundedSender<DiscoveryEvent>,
) -> JoinHandle<Result<()>> {
    tokio::task::spawn_blocking(move || {
        let callback_error = Arc::new(Mutex::new(None::<String>));

        let mut services = advertisements
            .iter()
            .map(|advertisement| {
                let kind = advertisement.kind;
                let requested_name = advertisement.name.clone();
                let callback_error_writer = callback_error.clone();
                let callback_cancellation = cancellation.clone();
                let callback_events = events.clone();
                let mut service = build_service(advertisement)?;
                service.set_registered_callback(Box::new(move |result, _context| match result {
                    Ok(registration) if registration.name() == &requested_name => {
                        let _ = callback_events.send(DiscoveryEvent::Registered {
                            kind,
                            requested_name: requested_name.clone(),
                            registered_name: registration.name().clone(),
                        });
                    }
                    Ok(registration) => {
                        let message = format!(
                            "mDNS changed the requested service name from {:?} to {:?}",
                            requested_name,
                            registration.name()
                        );
                        record_callback_error(&callback_error_writer, &message);
                        let _ = callback_events
                            .send(DiscoveryEvent::RegistrationFailed { kind, message });
                        callback_cancellation.cancel();
                    }
                    Err(err) => {
                        let message = err.to_string();
                        record_callback_error(&callback_error_writer, &message);
                        let _ = callback_events
                            .send(DiscoveryEvent::RegistrationFailed { kind, message });
                        callback_cancellation.cancel();
                    }
                }));
                Ok(service)
            })
            .collect::<Result<Vec<_>>>()?;

        let event_loop = services[0].register().map_err(|err| {
            let message = format!("failed to register {}: {err}", advertisements[0].name);
            let _ = events.send(DiscoveryEvent::RegistrationFailed {
                kind: advertisements[0].kind,
                message,
            });
            cancellation.cancel();
            anyhow!(err.to_string())
        })?;

        let second_event_loop = services[1].register().map_err(|err| {
            let message = format!("failed to register {}: {err}", advertisements[1].name);
            let _ = events.send(DiscoveryEvent::RegistrationFailed {
                kind: advertisements[1].kind,
                message,
            });
            cancellation.cancel();
            anyhow!(err.to_string())
        })?;

        while !cancellation.is_cancelled() {
            if let Err(err) = event_loop.poll(POLL_INTERVAL) {
                let message = format!("mDNS polling failed: {err}");
                let _ = events.send(DiscoveryEvent::RegistrationFailed {
                    kind: advertisements[0].kind,
                    message: message.clone(),
                });
                cancellation.cancel();
                return Err(anyhow!(message));
            }
            if let Err(err) = second_event_loop.poll(POLL_INTERVAL) {
                let message = format!("mDNS polling failed: {err}");
                let _ = events.send(DiscoveryEvent::RegistrationFailed {
                    kind: advertisements[1].kind,
                    message: message.clone(),
                });
                cancellation.cancel();
                return Err(anyhow!(message));
            }
        }

        drop(second_event_loop);
        drop(event_loop);
        drop(services);
        for advertisement in &advertisements {
            let _ = events.send(DiscoveryEvent::Stopped {
                kind: advertisement.kind,
            });
        }

        let callback_message = callback_error
            .lock()
            .map_err(|_| anyhow!("the mDNS callback error lock was poisoned"))?
            .take();
        if let Some(message) = callback_message {
            return Err(anyhow!(message));
        }

        Ok(())
    })
}

fn build_service(advertisement: &ServiceAdvertisement) -> Result<BackendMdnsService> {
    let service_type = ServiceType::new(advertisement.service_type(), "tcp")
        .context("failed to create the DNS-SD service type")?;
    let mut txt_record = BackendTxtRecord::new();
    for (key, value) in &advertisement.txt {
        txt_record
            .insert(key, value)
            .with_context(|| format!("failed to add the {key:?} DNS-SD TXT value"))?;
    }

    let mut service = BackendMdnsService::new(service_type, advertisement.port);
    service.set_name(&advertisement.name);
    service.set_network_interface(NetworkInterface::Unspec);
    service.set_txt_record(txt_record);
    Ok(service)
}

fn record_callback_error(slot: &Mutex<Option<String>>, message: &str) {
    if let Ok(mut slot) = slot.lock()
        && slot.is_none()
    {
        *slot = Some(message.to_owned());
    }
}

#[cfg(test)]
mod tests {
    use std::collections::BTreeSet;

    use tokio::time::{Duration, timeout};

    use super::super::{AccessControl, FeatureSet, ServiceKind};
    use super::*;

    #[tokio::test(flavor = "multi_thread")]
    #[ignore = "requires multicast networking"]
    async fn registers_and_stops_both_services() {
        let process_id = std::process::id();
        let config = DiscoveryConfig {
            receiver_name: format!("rsplay-test-{process_id}"),
            device_id: [
                0x02,
                0x00,
                0x00,
                (process_id >> 16) as u8,
                (process_id >> 8) as u8,
                process_id as u8,
            ],
            port: 49_152,
            public_key: "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef".into(),
            access_control: AccessControl::None,
            features: FeatureSet::default(),
        };

        let discovery = ZeroconfDiscovery::new(config).unwrap();
        let cancellation = CancellationToken::new();
        let (event_sender, mut events) = mpsc::unbounded_channel();
        let run_cancellation = cancellation.clone();
        let worker = tokio::spawn(Box::new(discovery).run(run_cancellation, event_sender));

        let registered = timeout(Duration::from_secs(5), async {
            let mut kinds = BTreeSet::new();
            while kinds.len() < 2 {
                match events.recv().await {
                    Some(DiscoveryEvent::Registered { kind, .. }) => {
                        kinds.insert(kind);
                    }
                    Some(DiscoveryEvent::RegistrationFailed { message, .. }) => {
                        panic!("mDNS registration failed: {message}");
                    }
                    Some(DiscoveryEvent::Stopped { kind }) => {
                        panic!("{kind:?} stopped before registration completed");
                    }
                    None => panic!("the mDNS event channel closed unexpectedly"),
                }
            }
            kinds
        })
        .await
        .expect("both services should register within five seconds");

        assert_eq!(
            registered,
            BTreeSet::from([ServiceKind::Airplay, ServiceKind::Raop])
        );

        cancellation.cancel();
        timeout(Duration::from_secs(2), worker)
            .await
            .expect("the mDNS workers should stop promptly")
            .expect("the discovery task should not panic")
            .expect("the discovery backend should stop cleanly");
    }
}
