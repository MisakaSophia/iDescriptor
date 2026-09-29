// SPDX-FileCopyrightText: 2026 Uncore <https://github.com/uncor3>
// SPDX-License-Identifier: AGPL-3.0-or-later

//! iDescriptor-specific AirPlay receiver integration.
//!
//! The reusable AirPlay protocol implementation lives in the external
//! `rsplay` crate. This module owns application concerns such as discovery,
//! persistent pairing state, and GStreamer playback.

// Some configuration helpers remain useful to the discovery module's tests,
// even though the application currently supplies protocol-generated records.
#[allow(dead_code, unused_imports)]
mod discovery;
mod pairing;
mod playback;
mod receiver;

pub(crate) use pairing::PersistentPairingStore;
pub(crate) use playback::GstreamerPlayback;
pub(crate) use receiver::{Receiver, ReceiverConfig, ReceiverEvent};
