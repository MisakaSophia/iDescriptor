// SPDX-FileCopyrightText: 2026 Uncore <https://github.com/uncor3>
// SPDX-License-Identifier: GPL-3.0-or-later

//! Persistent AirPlay accessory identity and controller pairing keys.

use std::{
    collections::BTreeMap,
    fs,
    path::{Path, PathBuf},
    sync::RwLock,
};

use anyhow::{Context, Result};
use directories::ProjectDirs;
use log::error;
use rand::{RngCore, rngs::OsRng};
use rsplay::PairingStore;
use serde::{Deserialize, Serialize};
use sha2::{Digest, Sha256};

#[derive(Debug, Default, Deserialize, Serialize)]
#[serde(rename_all = "camelCase")]
struct PairingData {
    identity_seed: Option<[u8; 32]>,
    #[serde(default)]
    controllers: BTreeMap<String, [u8; 32]>,
}

/// JSON-backed pairing storage with a stable accessory identity.
pub struct PersistentPairingStore {
    path: PathBuf,
    data: RwLock<PairingData>,
}

impl PersistentPairingStore {
    /// Open the standard per-user iDescriptor pairing store.
    pub fn open_default() -> Result<Self> {
        let project_dirs = ProjectDirs::from("com", "uncore", "iDescriptor")
            .context("the operating system did not provide a configuration directory")?;
        Self::open(project_dirs.config_dir().join("rsplay-pairings.json"))
    }

    /// Open a pairing store and create an accessory identity on first use.
    pub fn open(path: impl AsRef<Path>) -> Result<Self> {
        let path = path.as_ref().to_path_buf();
        let mut data = if path.exists() {
            let bytes = fs::read(&path)
                .with_context(|| format!("failed to read pairing store {}", path.display()))?;
            serde_json::from_slice(&bytes)
                .with_context(|| format!("failed to parse pairing store {}", path.display()))?
        } else {
            PairingData::default()
        };

        let created_identity = data.identity_seed.is_none();
        if created_identity {
            let mut seed = [0_u8; 32];
            OsRng.fill_bytes(&mut seed);
            data.identity_seed = Some(seed);
        }

        let store = Self {
            path,
            data: RwLock::new(data),
        };
        if created_identity {
            let data = store
                .data
                .read()
                .map_err(|_| anyhow::anyhow!("the pairing store lock was poisoned"))?;
            store.save(&data)?;
        }
        Ok(store)
    }

    /// Stable locally-administered MAC derived from the accessory identity.
    pub fn device_id(&self) -> [u8; 6] {
        let seed = self
            .data
            .read()
            .ok()
            .and_then(|data| data.identity_seed)
            .unwrap_or([0_u8; 32]);
        let digest = Sha256::digest(seed);
        let mut device_id = [0_u8; 6];
        device_id.copy_from_slice(&digest[..6]);
        device_id[0] = (device_id[0] | 0x02) & !0x01;
        device_id
    }

    fn save(&self, data: &PairingData) -> Result<()> {
        if let Some(parent) = self.path.parent() {
            fs::create_dir_all(parent).with_context(|| {
                format!("failed to create pairing directory {}", parent.display())
            })?;
        }
        let bytes = serde_json::to_vec_pretty(data).context("failed to serialize pairing data")?;
        fs::write(&self.path, bytes)
            .with_context(|| format!("failed to save pairing store {}", self.path.display()))
    }

    fn update(&self, change: impl FnOnce(&mut PairingData)) {
        let Ok(mut data) = self.data.write() else {
            error!("The rsplay pairing store lock was poisoned");
            return;
        };
        change(&mut data);
        if let Err(err) = self.save(&data) {
            error!("Failed to persist rsplay pairing data: {err:#}");
        }
    }
}

impl PairingStore for PersistentPairingStore {
    fn get(&self, device_id: &str) -> Option<[u8; 32]> {
        self.data.read().ok()?.controllers.get(device_id).copied()
    }

    fn put(&self, device_id: &str, public_key: [u8; 32]) {
        self.update(|data| {
            data.controllers.insert(device_id.to_owned(), public_key);
        });
    }

    fn remove(&self, device_id: &str) {
        self.update(|data| {
            data.controllers.remove(device_id);
        });
    }

    fn has_any_pairing(&self) -> bool {
        self.data
            .read()
            .map(|data| !data.controllers.is_empty())
            .unwrap_or(false)
    }

    fn load_identity(&self) -> Option<[u8; 32]> {
        self.data.read().ok()?.identity_seed
    }

    fn save_identity(&self, seed: [u8; 32]) {
        self.update(|data| data.identity_seed = Some(seed));
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn persists_identity_and_controller_keys() {
        let path = std::env::temp_dir().join(format!(
            "rsplay-pairing-test-{}-{}.json",
            std::process::id(),
            rand::random::<u64>()
        ));
        let store = PersistentPairingStore::open(&path).unwrap();
        let device_id = store.device_id();
        store.put("controller", [7; 32]);
        drop(store);

        let reopened = PersistentPairingStore::open(&path).unwrap();
        assert_eq!(reopened.device_id(), device_id);
        assert_eq!(reopened.get("controller"), Some([7; 32]));

        let _ = fs::remove_file(path);
    }
}
