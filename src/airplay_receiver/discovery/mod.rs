// SPDX-FileCopyrightText: 2026 Uncore <https://github.com/uncor3>
// SPDX-License-Identifier: GPL-3.0-or-later

//! AirPlay and RAOP DNS-SD advertisements.

mod txt;
mod zeroconf_backend;

use std::{collections::BTreeMap, future::Future, pin::Pin};

use anyhow::{Result, bail};
use tokio::sync::mpsc;
use tokio_util::sync::CancellationToken;

pub use txt::{AIRPLAY_PROTOCOL_VERSION, AIRPLAY_SOURCE_VERSION, RSPLAY_MODEL};
pub use zeroconf_backend::ZeroconfDiscovery;

/// The two DNS-SD services published by an AirPlay receiver.
#[derive(Clone, Copy, Debug, Eq, Ord, PartialEq, PartialOrd)]
pub enum ServiceKind {
    Airplay,
    Raop,
}

impl ServiceKind {
    pub const fn service_type(self) -> &'static str {
        match self {
            Self::Airplay => "airplay",
            Self::Raop => "raop",
        }
    }
}

/// Controls the access-related values published in DNS-SD TXT records.
#[derive(Clone, Copy, Debug, Default, Eq, PartialEq)]
pub enum AccessControl {
    #[default]
    None,
    OnscreenPin,
    Password,
    TransientPin,
}

impl AccessControl {
    const fn password_required(self) -> bool {
        !matches!(self, Self::None)
    }

    const fn raop_status_flags(self) -> &'static str {
        match self {
            Self::None => "0x4",
            Self::OnscreenPin => "0x8c",
            Self::Password | Self::TransientPin => "0x84",
        }
    }
}

/// AirPlay feature bits which affect discovery.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct FeatureSet {
    pub hls: bool,
    pub h265: bool,
    pub legacy_pairing: bool,
}

impl Default for FeatureSet {
    fn default() -> Self {
        Self {
            hls: false,
            h265: true,
            legacy_pairing: true,
        }
    }
}

impl FeatureSet {
    /// Return the 64-bit feature mask advertised to AirPlay clients.
    pub fn bits(self) -> u64 {
        let mut bits = 0_u64;

        // Keep this list aligned with the documented UxPlay v1.73.6 defaults.
        for bit in [
            1_u8, 2, 5, 6, 7, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 25, 28, 30,
        ] {
            bits |= 1_u64 << bit;
        }

        if self.hls {
            bits |= (1_u64 << 0) | (1_u64 << 4);
        }
        if self.legacy_pairing {
            bits |= 1_u64 << 27;
        }
        if self.h265 {
            bits |= 1_u64 << 42;
        }

        bits
    }

    fn dns_sd_value(self) -> String {
        let bits = self.bits();
        format!("0x{:X},0x{:X}", bits as u32, (bits >> 32) as u32)
    }
}

/// Configuration needed to advertise an rsplay receiver.
#[derive(Clone, Debug, Eq, PartialEq)]
pub struct DiscoveryConfig {
    pub receiver_name: String,
    pub device_id: [u8; 6],
    pub port: u16,
    /// Hex-encoded Ed25519 public key published as the `pk` TXT value.
    pub public_key: String,
    pub access_control: AccessControl,
    pub features: FeatureSet,
}

impl DiscoveryConfig {
    pub fn validate(&self) -> Result<()> {
        if self.receiver_name.trim().is_empty() {
            bail!("the receiver name must not be empty");
        }
        if self.port == 0 {
            bail!("the advertised AirPlay port must not be zero");
        }
        if self.public_key.len() != 64
            || !self.public_key.bytes().all(|byte| byte.is_ascii_hexdigit())
        {
            bail!("the AirPlay public key must be a 64-character hexadecimal string");
        }
        Ok(())
    }

    /// Build deterministic descriptions of both services before registering.
    pub fn advertisements(&self) -> Result<[ServiceAdvertisement; 2]> {
        self.validate()?;
        Ok([
            txt::airplay_advertisement(self),
            txt::raop_advertisement(self),
        ])
    }

    pub fn device_id_with_colons(&self) -> String {
        self.device_id
            .iter()
            .map(|byte| format!("{byte:02X}"))
            .collect::<Vec<_>>()
            .join(":")
    }

    pub fn device_id_compact(&self) -> String {
        self.device_id
            .iter()
            .map(|byte| format!("{byte:02X}"))
            .collect()
    }
}

/// A platform-independent DNS-SD registration description.
#[derive(Clone, Debug, Eq, PartialEq)]
pub struct ServiceAdvertisement {
    pub kind: ServiceKind,
    pub name: String,
    pub port: u16,
    pub txt: BTreeMap<String, String>,
}

impl ServiceAdvertisement {
    pub const fn service_type(&self) -> &'static str {
        self.kind.service_type()
    }
}

/// Lifecycle notifications from the discovery backend.
#[derive(Clone, Debug, Eq, PartialEq)]
pub enum DiscoveryEvent {
    Registered {
        kind: ServiceKind,
        requested_name: String,
        registered_name: String,
    },
    RegistrationFailed {
        kind: ServiceKind,
        message: String,
    },
    Stopped {
        kind: ServiceKind,
    },
}

pub type DiscoveryFuture = Pin<Box<dyn Future<Output = Result<()>> + Send + 'static>>;

/// Internal boundary which keeps platform DNS-SD types out of rsplay's API.
pub trait DiscoveryBackend: Send + 'static {
    fn run(
        self: Box<Self>,
        cancellation: CancellationToken,
        events: mpsc::UnboundedSender<DiscoveryEvent>,
    ) -> DiscoveryFuture;
}

#[cfg(test)]
mod tests {
    use super::*;

    fn config() -> DiscoveryConfig {
        DiscoveryConfig {
            receiver_name: "iDescriptor@rsplay".into(),
            device_id: [0x00, 0x11, 0x22, 0xAA, 0xBB, 0xCC],
            port: 7000,
            public_key: "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef".into(),
            access_control: AccessControl::None,
            features: FeatureSet::default(),
        }
    }

    #[test]
    fn formats_device_id_for_both_service_names() {
        let config = config();
        assert_eq!(config.device_id_with_colons(), "00:11:22:AA:BB:CC");
        assert_eq!(config.device_id_compact(), "001122AABBCC");
    }

    #[test]
    fn matches_uxplay_default_feature_bits() {
        let features = FeatureSet {
            hls: false,
            h265: false,
            legacy_pairing: true,
        };
        assert_eq!(features.bits(), 0x5A7F_FEE6);
        assert_eq!(features.dns_sd_value(), "0x5A7FFEE6,0x0");
    }

    #[test]
    fn adds_hls_and_h265_feature_bits() {
        let features = FeatureSet {
            hls: true,
            h265: true,
            legacy_pairing: true,
        };
        assert_eq!(features.bits(), 0x400_5A7F_FEF7);
        assert_eq!(features.dns_sd_value(), "0x5A7FFEF7,0x400");
    }

    #[test]
    fn validates_inputs_before_touching_mdns() {
        let mut config = config();
        config.port = 0;
        assert!(config.validate().is_err());

        config.port = 7000;
        config.public_key = "not-a-key".into();
        assert!(config.validate().is_err());
    }
}
