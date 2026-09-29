// SPDX-FileCopyrightText: 2026 Uncore <https://github.com/uncor3>
// SPDX-License-Identifier: GPL-3.0-or-later

use std::collections::BTreeMap;

use super::{DiscoveryConfig, ServiceAdvertisement, ServiceKind};

pub const RSPLAY_MODEL: &str = "AppleTV3,2";
pub const AIRPLAY_SOURCE_VERSION: &str = "220.68";
pub const AIRPLAY_PROTOCOL_VERSION: &str = "2";
const AIRPLAY_PAIRING_ID: &str = "2e388006-13ba-4041-9a67-25dd4a43d536";

pub(super) fn airplay_advertisement(config: &DiscoveryConfig) -> ServiceAdvertisement {
    let mut txt = BTreeMap::new();
    insert(&mut txt, "deviceid", config.device_id_with_colons());
    insert(&mut txt, "features", config.features.dns_sd_value());
    insert(
        &mut txt,
        "pw",
        bool_string(config.access_control.password_required()),
    );
    insert(&mut txt, "flags", "0x4");
    insert(&mut txt, "model", RSPLAY_MODEL);
    insert(&mut txt, "pk", config.public_key.clone());
    insert(&mut txt, "pi", AIRPLAY_PAIRING_ID);
    insert(&mut txt, "srcvers", AIRPLAY_SOURCE_VERSION);
    insert(&mut txt, "vv", AIRPLAY_PROTOCOL_VERSION);

    ServiceAdvertisement {
        kind: ServiceKind::Airplay,
        name: config.receiver_name.clone(),
        port: config.port,
        txt,
    }
}

pub(super) fn raop_advertisement(config: &DiscoveryConfig) -> ServiceAdvertisement {
    let mut txt = BTreeMap::new();
    insert(&mut txt, "ch", "2");
    insert(&mut txt, "cn", "0,1,3");
    insert(&mut txt, "da", "true");
    insert(&mut txt, "et", "0,3,5");
    insert(&mut txt, "vv", AIRPLAY_PROTOCOL_VERSION);
    insert(&mut txt, "ft", config.features.dns_sd_value());
    insert(&mut txt, "am", RSPLAY_MODEL);
    insert(&mut txt, "md", "0,1,2");
    insert(&mut txt, "rhd", "5.6.0.0");
    insert(
        &mut txt,
        "pw",
        bool_string(config.access_control.password_required()),
    );
    insert(&mut txt, "sf", config.access_control.raop_status_flags());
    insert(&mut txt, "sr", "44100");
    insert(&mut txt, "ss", "16");
    insert(&mut txt, "sv", "false");
    insert(&mut txt, "tp", "UDP");
    insert(&mut txt, "txtvers", "1");
    insert(&mut txt, "vs", AIRPLAY_SOURCE_VERSION);
    insert(&mut txt, "vn", "65537");
    insert(&mut txt, "pk", config.public_key.clone());

    ServiceAdvertisement {
        kind: ServiceKind::Raop,
        name: format!("{}@{}", config.device_id_compact(), config.receiver_name),
        port: config.port,
        txt,
    }
}

fn insert(map: &mut BTreeMap<String, String>, key: &str, value: impl Into<String>) {
    map.insert(key.to_owned(), value.into());
}

const fn bool_string(value: bool) -> &'static str {
    if value { "true" } else { "false" }
}

#[cfg(test)]
mod tests {
    use super::super::{AccessControl, FeatureSet, ServiceKind};
    use super::*;

    fn config(access_control: AccessControl) -> DiscoveryConfig {
        DiscoveryConfig {
            receiver_name: "iDescriptor@rsplay".into(),
            device_id: [0x00, 0x11, 0x22, 0xAA, 0xBB, 0xCC],
            port: 7000,
            public_key: "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef".into(),
            access_control,
            features: FeatureSet {
                hls: false,
                h265: false,
                legacy_pairing: true,
            },
        }
    }

    #[test]
    fn builds_airplay_record_compatible_with_uxplay() {
        let advertisement = airplay_advertisement(&config(AccessControl::None));
        assert_eq!(advertisement.kind, ServiceKind::Airplay);
        assert_eq!(advertisement.name, "iDescriptor@rsplay");
        assert_eq!(advertisement.txt["deviceid"], "00:11:22:AA:BB:CC");
        assert_eq!(advertisement.txt["features"], "0x5A7FFEE6,0x0");
        assert_eq!(advertisement.txt["model"], "AppleTV3,2");
        assert_eq!(advertisement.txt["pw"], "false");
        assert_eq!(advertisement.txt.len(), 9);
    }

    #[test]
    fn builds_raop_record_compatible_with_uxplay() {
        let advertisement = raop_advertisement(&config(AccessControl::None));
        assert_eq!(advertisement.kind, ServiceKind::Raop);
        assert_eq!(advertisement.name, "001122AABBCC@iDescriptor@rsplay");
        assert_eq!(advertisement.txt["cn"], "0,1,3");
        assert_eq!(advertisement.txt["et"], "0,3,5");
        assert_eq!(advertisement.txt["ft"], "0x5A7FFEE6,0x0");
        assert_eq!(advertisement.txt["sf"], "0x4");
        assert_eq!(advertisement.txt.len(), 19);
    }

    #[test]
    fn advertises_access_control_consistently() {
        let advertisement = raop_advertisement(&config(AccessControl::OnscreenPin));
        assert_eq!(advertisement.txt["pw"], "true");
        assert_eq!(advertisement.txt["sf"], "0x8c");

        let advertisement = raop_advertisement(&config(AccessControl::Password));
        assert_eq!(advertisement.txt["pw"], "true");
        assert_eq!(advertisement.txt["sf"], "0x84");
    }
}
