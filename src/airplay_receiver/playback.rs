// SPDX-FileCopyrightText: 2026 Uncore <https://github.com/uncor3>
// SPDX-License-Identifier: GPL-3.0-or-later

//! GStreamer playback for decoded AirPlay audio and mirrored video.

use std::{
    ffi::c_void,
    sync::{
        Arc,
        atomic::{AtomicU32, AtomicUsize, Ordering},
    },
};

use anyhow::{Context, Result, anyhow};
use gstreamer as gst;
use gstreamer::prelude::*;
use gstreamer_app as gst_app;
use log::{debug, error, warn};
use rsplay::{
    AudioCodec, AudioFormat, AudioHandler, AudioSession, PacketKind, VideoHandler, VideoPacket,
    VideoSession,
};
use tokio::sync::mpsc;

use super::receiver::ReceiverEvent;

const PCM_AUDIO_PIPELINE: &str = concat!(
    "appsrc name=audio_source is-live=true format=time block=false ",
    "! queue ! audioconvert ! audioresample ! volume name=master_volume ",
    "! level ! autoaudiosink sync=false"
);
const AAC_ELD_AUDIO_PIPELINE: &str = concat!(
    "appsrc name=audio_source is-live=true format=time block=false ",
    "! queue ! avdec_aac ! audioconvert ! audioresample ",
    "! volume name=master_volume ! level ! autoaudiosink sync=false"
);
const ALAC_AUDIO_PIPELINE: &str = concat!(
    "appsrc name=audio_source is-live=true format=time block=false ",
    "! queue ! avdec_alac ! audioconvert ! audioresample ",
    "! volume name=master_volume ! level ! autoaudiosink sync=false"
);

// AirPlay ALAC magic cookie: 44100 Hz, 16-bit stereo, 352 samples per frame.
const ALAC_CODEC_DATA: [u8; 36] = [
    0x00, 0x00, 0x00, 0x24, 0x61, 0x6c, 0x61, 0x63, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x60,
    0x00, 0x10, 0x28, 0x0a, 0x0e, 0x02, 0x00, 0xff, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0xac, 0x44,
];

/// GStreamer-backed AirPlay playback and connection event adapter.
pub struct GstreamerPlayback {
    video_item: usize,
    volume_bits: Arc<AtomicU32>,
    connections: Arc<AtomicUsize>,
    events: mpsc::UnboundedSender<ReceiverEvent>,
}

impl GstreamerPlayback {
    /// Check that GStreamer and the Qt Quick GL sink are available.
    pub fn qml_sink_available() -> bool {
        if let Err(err) = gst::init() {
            error!("Failed to initialize GStreamer: {err}");
            return false;
        }
        match gst::ElementFactory::make("qml6glsink").build() {
            Ok(sink) => {
                drop(sink);
                true
            }
            Err(err) => {
                error!("The qml6glsink GStreamer plugin is unavailable: {err}");
                false
            }
        }
    }

    /// Validate GStreamer and retain the QML-owned video item pointer.
    pub fn new(
        video_item: usize,
        events: mpsc::UnboundedSender<ReceiverEvent>,
    ) -> Result<Arc<Self>> {
        if video_item == 0 {
            return Err(anyhow!("the AirPlay video item is unavailable"));
        }
        gst::init().context("failed to initialize GStreamer")?;
        if !Self::qml_sink_available() {
            return Err(anyhow!("the qml6glsink GStreamer plugin is unavailable"));
        }

        Ok(Arc::new(Self {
            video_item,
            volume_bits: Arc::new(AtomicU32::new(1.0_f32.to_bits())),
            connections: Arc::new(AtomicUsize::new(0)),
            events,
        }))
    }

    /// Initialize playback for an audio-only receiver without requiring a QML video sink.
    pub fn new_audio_only(events: mpsc::UnboundedSender<ReceiverEvent>) -> Result<Arc<Self>> {
        gst::init().context("failed to initialize GStreamer")?;
        Ok(Arc::new(Self {
            video_item: 0,
            volume_bits: Arc::new(AtomicU32::new(1.0_f32.to_bits())),
            connections: Arc::new(AtomicUsize::new(0)),
            events,
        }))
    }

    /// Set application master volume in the inclusive range 0.0 to 1.0.
    pub fn set_volume(&self, volume: f32) {
        self.volume_bits
            .store(volume.clamp(0.0, 1.0).to_bits(), Ordering::Relaxed);
    }

    fn send_error(&self, message: impl Into<String>) {
        let _ = self.events.send(ReceiverEvent::Error(message.into()));
    }
}

impl AudioHandler for GstreamerPlayback {
    fn audio_init(&self, format: AudioFormat) -> Box<dyn AudioSession> {
        match GstAudioSession::new(format, self.volume_bits.clone(), self.events.clone()) {
            Ok(session) => Box::new(session),
            Err(err) => {
                self.send_error(format!("failed to start AirPlay audio: {err:#}"));
                Box::new(DiscardAudioSession)
            }
        }
    }

    fn on_client_connected(&self, address: &str) {
        if self.connections.fetch_add(1, Ordering::AcqRel) == 0 {
            let _ = self.events.send(ReceiverEvent::ClientConnected {
                address: address.to_owned(),
            });
        }
    }

    fn on_client_disconnected(&self, _address: &str) {
        let previous = self
            .connections
            .fetch_update(Ordering::AcqRel, Ordering::Acquire, |count| {
                Some(count.saturating_sub(1))
            })
            .unwrap_or(0);
        if previous == 1 {
            let _ = self.events.send(ReceiverEvent::ClientDisconnected);
        }
    }

    fn on_client_info(&self, device_id: &str, model: &str, name: &str) {
        let _ = self.events.send(ReceiverEvent::ClientDetails {
            device_id: device_id.to_owned(),
            model: model.to_owned(),
            name: name.to_owned(),
        });
    }

    fn on_error(&self, err: &rsplay::ShairplayError) {
        self.send_error(err.to_string());
    }
}

impl VideoHandler for GstreamerPlayback {
    fn video_init(&self) -> Box<dyn VideoSession> {
        // The mirror TCP stream outlives the RTSP control connection when iOS
        // tears down audio (type 96). Count it independently so closing that
        // control socket cannot make QML report a full client disconnect.
        self.connections.fetch_add(1, Ordering::AcqRel);
        let session: Box<dyn VideoSession> = match GstVideoSession::new(self.video_item) {
            Ok(session) => Box::new(session),
            Err(err) => {
                self.send_error(format!("failed to start AirPlay video: {err:#}"));
                Box::new(DiscardVideoSession)
            }
        };
        Box::new(TrackedVideoSession {
            inner: session,
            connections: self.connections.clone(),
            events: self.events.clone(),
            ended: false,
        })
    }
}

struct TrackedVideoSession {
    inner: Box<dyn VideoSession>,
    connections: Arc<AtomicUsize>,
    events: mpsc::UnboundedSender<ReceiverEvent>,
    ended: bool,
}

impl TrackedVideoSession {
    fn finish(&mut self) {
        if self.ended {
            return;
        }
        self.ended = true;
        let previous = self
            .connections
            .fetch_update(Ordering::AcqRel, Ordering::Acquire, |count| {
                Some(count.saturating_sub(1))
            })
            .unwrap_or(0);
        if previous == 1 {
            let _ = self.events.send(ReceiverEvent::ClientDisconnected);
        }
    }
}

impl VideoSession for TrackedVideoSession {
    fn on_video(&mut self, packet: VideoPacket) {
        self.inner.on_video(packet);
    }

    fn on_resolution_changed(&mut self, width: u32, height: u32) {
        self.inner.on_resolution_changed(width, height);
    }

    fn on_video_end(&mut self) {
        self.inner.on_video_end();
        self.finish();
    }
}

impl Drop for TrackedVideoSession {
    fn drop(&mut self) {
        self.finish();
    }
}

struct GstAudioSession {
    codec: AudioCodec,
    pipeline: gst::Pipeline,
    appsrc: gst_app::AppSrc,
    volume: gst::Element,
    volume_bits: Arc<AtomicU32>,
    applied_volume_bits: u32,
    buffers_pushed: u64,
    events: mpsc::UnboundedSender<ReceiverEvent>,
    active: bool,
}

impl GstAudioSession {
    fn new(
        format: AudioFormat,
        volume_bits: Arc<AtomicU32>,
        events: mpsc::UnboundedSender<ReceiverEvent>,
    ) -> Result<Self> {
        if format.codec == AudioCodec::AacEld
            && (format.sample_rate != 44_100 || format.channels != 2)
        {
            return Err(anyhow!(
                "unsupported AAC-ELD format: {} Hz, {} channels",
                format.sample_rate,
                format.channels
            ));
        }
        let description = match format.codec {
            AudioCodec::Pcm => PCM_AUDIO_PIPELINE,
            AudioCodec::Alac => ALAC_AUDIO_PIPELINE,
            AudioCodec::AacEld => AAC_ELD_AUDIO_PIPELINE,
        };
        let pipeline = pipeline_from_description(description, "audio")?;
        let appsrc = appsrc(&pipeline, "audio_source")?;
        let volume = pipeline
            .by_name("master_volume")
            .context("the AirPlay audio pipeline has no volume element")?;
        let caps = match format.codec {
            AudioCodec::Pcm => gst::Caps::builder("audio/x-raw")
                .field("format", "F32LE")
                .field("layout", "interleaved")
                .field("rate", format.sample_rate as i32)
                .field("channels", format.channels as i32)
                .build(),
            AudioCodec::Alac => gst::Caps::builder("audio/x-alac")
                .field("mpegversion", 4_i32)
                .field("stream-format", "raw")
                .field("rate", format.sample_rate as i32)
                .field("channels", format.channels as i32)
                .field("codec_data", gst::Buffer::from_slice(ALAC_CODEC_DATA))
                .build(),
            AudioCodec::AacEld => gst::Caps::builder("audio/mpeg")
                .field("mpegversion", 4_i32)
                .field("stream-format", "raw")
                .field("rate", format.sample_rate as i32)
                .field("channels", format.channels as i32)
                .field(
                    "codec_data",
                    gst::Buffer::from_slice([0xf8_u8, 0xe8, 0x50, 0x00]),
                )
                .build(),
        };
        appsrc.set_caps(Some(&caps));

        let applied_volume_bits = volume_bits.load(Ordering::Relaxed);
        volume.set_property("volume", f32::from_bits(applied_volume_bits) as f64);
        let clock = gst::SystemClock::obtain();
        clock.set_property("clock-type", gst::ClockType::Realtime);
        pipeline.use_clock(Some(&clock));
        start_pipeline(&pipeline, "audio")?;
        debug!(
            "Started AirPlay audio pipeline: {:?}, {} Hz, {} channels",
            format.codec, format.sample_rate, format.channels
        );
        Ok(Self {
            codec: format.codec,
            pipeline,
            appsrc,
            volume,
            volume_bits,
            applied_volume_bits,
            buffers_pushed: 0,
            events,
            active: false,
        })
    }

    fn apply_volume(&mut self) {
        let current = self.volume_bits.load(Ordering::Relaxed);
        if current != self.applied_volume_bits {
            self.volume
                .set_property("volume", f32::from_bits(current) as f64);
            self.applied_volume_bits = current;
        }
    }

    fn push_buffer(&mut self, bytes: Vec<u8>) {
        match self.appsrc.push_buffer(gst::Buffer::from_mut_slice(bytes)) {
            Ok(_) => self.buffers_pushed += 1,
            Err(err) => error!("GStreamer rejected an AirPlay audio buffer: {err:?}"),
        }
        poll_bus(&self.pipeline, "audio");
    }
}

impl AudioSession for GstAudioSession {
    fn set_active(&mut self, active: bool) {
        if self.active == active {
            return;
        }
        self.active = active;
        let _ = self
            .events
            .send(ReceiverEvent::AudioActivityChanged { active });
    }

    fn audio_process(&mut self, samples: &[f32]) {
        if samples.is_empty() {
            return;
        }
        if self.codec != AudioCodec::Pcm {
            warn!(
                "Ignored PCM samples delivered to a {:?} audio session",
                self.codec
            );
            return;
        }
        self.apply_volume();
        let first_buffer = self.buffers_pushed == 0;
        self.push_buffer(bytemuck::cast_slice(samples).to_vec());
        if first_buffer && self.buffers_pushed == 1 {
            let peak = samples
                .iter()
                .fold(0.0_f32, |peak, sample| peak.max(sample.abs()));
            debug!(
                "Pushed first decoded AirPlay PCM buffer: {} samples, peak={peak:.4}",
                samples.len()
            );
        }
    }

    fn audio_process_encoded(&mut self, data: &[u8]) {
        if data.is_empty() {
            return;
        }
        if !matches!(self.codec, AudioCodec::Alac | AudioCodec::AacEld) {
            warn!(
                "Ignored encoded audio data delivered to a {:?} audio session",
                self.codec
            );
            return;
        }
        self.apply_volume();
        let first_buffer = self.buffers_pushed == 0;
        self.push_buffer(data.to_vec());
        if first_buffer && self.buffers_pushed == 1 {
            debug!(
                "Pushed first decrypted AirPlay {:?} access unit: {} bytes",
                self.codec,
                data.len()
            );
        }
    }

    fn audio_flush(&mut self) {
        let _ = self.pipeline.send_event(gst::event::FlushStart::new());
        let _ = self.pipeline.send_event(gst::event::FlushStop::new(true));
    }

    fn set_volume(&mut self, volume: f32) {
        let volume = volume.clamp(0.0, 1.0);
        self.volume_bits.store(volume.to_bits(), Ordering::Relaxed);
        self.volume.set_property("volume", volume as f64);
        self.applied_volume_bits = volume.to_bits();
    }
}

impl Drop for GstAudioSession {
    fn drop(&mut self) {
        self.set_active(false);
        stop_pipeline(&self.pipeline, Some(&self.appsrc), "audio");
    }
}

struct GstVideoSession {
    video_item: usize,
    renderer: Option<GstVideoRenderer>,
    codec: Option<VideoCodec>,
    pending_parameter_sets: Vec<u8>,
    first_remote_timestamp: Option<u64>,
}

struct GstVideoRenderer {
    pipeline: gst::Pipeline,
    appsrc: gst_app::AppSrc,
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
enum VideoCodec {
    H264,
    H265,
}

impl GstVideoSession {
    fn new(video_item: usize) -> Result<Self> {
        if video_item == 0 {
            return Err(anyhow!("the AirPlay video item is unavailable"));
        }
        Ok(Self {
            video_item,
            renderer: None,
            codec: None,
            pending_parameter_sets: Vec::new(),
            first_remote_timestamp: None,
        })
    }

    fn create_renderer(&self, codec: VideoCodec) -> Result<GstVideoRenderer> {
        let description = video_pipeline_description(codec);
        debug!("rsplay GStreamer video pipeline: {description}");
        let pipeline = pipeline_from_description(&description, "video")?;
        let appsrc = appsrc(&pipeline, "video_source")?;
        let sink = pipeline
            .by_name("qml_sink")
            .context("the AirPlay video pipeline has no qml6glsink")?;
        // qml6glsink treats this as a borrowed QQuickItem pointer owned by QML.
        sink.set_property("widget", self.video_item as *mut c_void);

        let caps = gst::Caps::builder(codec.media_type())
            .field("stream-format", "byte-stream")
            .field("alignment", "au")
            .build();
        appsrc.set_caps(Some(&caps));

        let clock = gst::SystemClock::obtain();
        clock.set_property("clock-type", gst::ClockType::Realtime);
        pipeline.use_clock(Some(&clock));

        pipeline
            .set_state(gst::State::Ready)
            .map_err(|err| anyhow!("failed to ready the AirPlay video pipeline: {err:?}"))?;
        start_pipeline(&pipeline, "video")?;
        Ok(GstVideoRenderer { pipeline, appsrc })
    }

    fn configure(&mut self, codec: VideoCodec, _payload: &[u8]) -> Result<()> {
        if let Some(previous) = self.codec
            && previous != codec
        {
            return Err(anyhow!(
                "the AirPlay video codec changed from {previous:?} to {codec:?}"
            ));
        }
        if self.renderer.is_none() {
            self.renderer = Some(self.create_renderer(codec)?);
        }
        self.codec = Some(codec);
        // rsplay_core caches these sets and prepends them to the following
        // access unit. Retaining another copy here would send the
        // SPS/PPS twice to h264parse.
        self.pending_parameter_sets.clear();
        debug!("Configured the rsplay GStreamer pipeline for {codec:?}");
        Ok(())
    }

    fn push_payload(&mut self, payload: &[u8], timestamp: u64) -> Result<()> {
        if self.codec.is_none() {
            return Err(anyhow!(
                "received AirPlay video before its codec configuration"
            ));
        }
        let converted = nal_units_to_byte_stream(payload)?;
        let mut bytes = Vec::with_capacity(self.pending_parameter_sets.len() + converted.len());
        bytes.append(&mut self.pending_parameter_sets);
        bytes.extend_from_slice(&converted);
        let mut buffer = gst::Buffer::from_mut_slice(bytes);
        let remote_ns = timestamp;
        let first_remote_ns = *self.first_remote_timestamp.get_or_insert(remote_ns);
        if let Some(buffer) = buffer.get_mut() {
            buffer.set_pts(gst::ClockTime::from_nseconds(
                remote_ns.saturating_sub(first_remote_ns),
            ));
        }
        let renderer = self
            .renderer
            .as_ref()
            .context("the AirPlay video renderer is not configured")?;
        renderer
            .appsrc
            .push_buffer(buffer)
            .map_err(|err| anyhow!("GStreamer rejected an AirPlay video buffer: {err:?}"))?;
        poll_bus(&renderer.pipeline, "video");
        Ok(())
    }
}

impl VideoCodec {
    const fn media_type(self) -> &'static str {
        match self {
            Self::H264 => "video/x-h264",
            Self::H265 => "video/x-h265",
        }
    }

    const fn parser(self) -> &'static str {
        match self {
            Self::H264 => "h264parse",
            Self::H265 => "h265parse",
        }
    }
}

impl VideoSession for GstVideoSession {
    fn on_video(&mut self, packet: VideoPacket) {
        let result = match packet.kind {
            PacketKind::AvcC => self.configure(VideoCodec::H264, &packet.payload),
            PacketKind::HvcC => self.configure(VideoCodec::H265, &packet.payload),
            PacketKind::Payload => self.push_payload(&packet.payload, packet.timestamp),
            PacketKind::Plist | PacketKind::Other(_) => Ok(()),
        };
        if let Err(err) = result {
            warn!("Ignored an invalid AirPlay video packet: {err:#}");
        }
    }

    fn on_video_end(&mut self) {
        if let Some(renderer) = &self.renderer {
            stop_pipeline(&renderer.pipeline, Some(&renderer.appsrc), "video");
        }
    }

    fn on_video_pause(&mut self) {
        if let Some(renderer) = &self.renderer {
            let _ = renderer.pipeline.set_state(gst::State::Paused);
        }
    }

    fn on_video_resume(&mut self) {
        if let Some(renderer) = &self.renderer {
            let _ = renderer.pipeline.set_state(gst::State::Playing);
        }
    }
}

impl Drop for GstVideoSession {
    fn drop(&mut self) {
        if let Some(renderer) = &self.renderer {
            stop_pipeline(&renderer.pipeline, None, "video");
        }
    }
}

struct DiscardAudioSession;
impl AudioSession for DiscardAudioSession {
    fn audio_process(&mut self, _samples: &[f32]) {}
}

struct DiscardVideoSession;
impl VideoSession for DiscardVideoSession {
    fn on_video(&mut self, _packet: VideoPacket) {}
}

fn pipeline_from_description(description: &str, media: &str) -> Result<gst::Pipeline> {
    gst::parse::launch(description)
        .with_context(|| format!("failed to create the AirPlay {media} pipeline"))?
        .downcast::<gst::Pipeline>()
        .map_err(|_| anyhow!("the AirPlay {media} pipeline has an unexpected type"))
}

fn appsrc(pipeline: &gst::Pipeline, name: &str) -> Result<gst_app::AppSrc> {
    pipeline
        .by_name(name)
        .with_context(|| format!("the GStreamer pipeline has no {name}"))?
        .downcast::<gst_app::AppSrc>()
        .map_err(|_| anyhow!("the GStreamer element {name} is not an AppSrc"))
}

fn start_pipeline(pipeline: &gst::Pipeline, media: &str) -> Result<()> {
    pipeline
        .set_state(gst::State::Playing)
        .map_err(|err| anyhow!("failed to start the AirPlay {media} pipeline: {err:?}"))?;
    Ok(())
}

fn stop_pipeline(pipeline: &gst::Pipeline, appsrc: Option<&gst_app::AppSrc>, media: &str) {
    if let Some(appsrc) = appsrc {
        let _ = appsrc.end_of_stream();
    }
    if let Err(err) = pipeline.set_state(gst::State::Null) {
        warn!("Failed to stop the AirPlay {media} pipeline: {err:?}");
    }
}

fn poll_bus(pipeline: &gst::Pipeline, media: &str) {
    let Some(bus) = pipeline.bus() else {
        return;
    };
    while let Some(message) = bus.pop() {
        match message.view() {
            gst::MessageView::Error(message) => error!(
                "AirPlay {media} pipeline error: {} ({:?})",
                message.error(),
                message.debug()
            ),
            gst::MessageView::Warning(message) => warn!(
                "AirPlay {media} pipeline warning: {} ({:?})",
                message.error(),
                message.debug()
            ),
            _ => {}
        }
    }
}

fn video_pipeline_description(codec: VideoCodec) -> String {
    let color_conversion = color_conversion_pipeline();
    format!(
        concat!(
            "appsrc name=video_source is-live=true format=time block=false ",
            "! queue ! {} ! decodebin ! {} ! videoscale ",
            "! queue leaky=upstream max-size-buffers=5 max-size-bytes=0 ",
            "max-size-time=500000000 ",
            "! glupload ! qml6glsink name=qml_sink qos=true sync=false"
        ),
        codec.parser(),
        color_conversion,
    )
}

fn color_conversion_pipeline() -> &'static str {
    // Enable full-range sRGB correction by default on Linux/BSD.
    // AppImage's GStreamer 1.20 baseline requires RGBA for GstGLQt6VideoItem.
    #[cfg(all(unix, not(target_vendor = "apple"), feature = "appimage"))]
    {
        "videoconvert ! video/x-raw,colorimetry=sRGB,format=RGBA ! videoconvert"
    }
    #[cfg(all(unix, not(target_vendor = "apple"), not(feature = "appimage")))]
    {
        "videoconvert ! video/x-raw,colorimetry=sRGB,format=RGB ! videoconvert"
    }
    #[cfg(not(all(unix, not(target_vendor = "apple"))))]
    {
        "videoconvert"
    }
}

fn nal_units_to_byte_stream(payload: &[u8]) -> Result<Vec<u8>> {
    if starts_with_annex_b_start_code(payload) {
        if annex_b_nal_units(payload)
            .iter()
            .any(|nal| nal.is_empty() || nal[0] & 0x80 != 0)
        {
            return Err(anyhow!(
                "AirPlay video decryption produced an invalid NAL unit"
            ));
        }
        return Ok(payload.to_vec());
    }

    let mut output = Vec::with_capacity(payload.len());
    let mut offset = 0;
    while offset < payload.len() {
        let length_bytes: [u8; 4] = payload
            .get(offset..offset + 4)
            .context("truncated AirPlay NAL length")?
            .try_into()
            .expect("the slice length was checked");
        let length = u32::from_be_bytes(length_bytes) as usize;
        offset += 4;
        let nal = payload
            .get(offset..offset + length)
            .context("AirPlay NAL length exceeds the video payload")?;
        if nal.is_empty() || nal[0] & 0x80 != 0 {
            return Err(anyhow!(
                "AirPlay video decryption produced an invalid NAL unit"
            ));
        }
        output.extend_from_slice(&[0, 0, 0, 1]);
        output.extend_from_slice(nal);
        offset += length;
    }
    Ok(output)
}

fn starts_with_annex_b_start_code(payload: &[u8]) -> bool {
    payload.starts_with(&[0, 0, 0, 1]) || payload.starts_with(&[0, 0, 1])
}

fn annex_b_nal_units(payload: &[u8]) -> Vec<&[u8]> {
    let mut starts = Vec::new();
    let mut offset = 0;
    while offset + 3 <= payload.len() {
        let start_code_len = if payload[offset..].starts_with(&[0, 0, 0, 1]) {
            4
        } else if payload[offset..].starts_with(&[0, 0, 1]) {
            3
        } else {
            offset += 1;
            continue;
        };
        starts.push((offset, start_code_len));
        offset += start_code_len;
    }

    starts
        .iter()
        .enumerate()
        .filter_map(|(index, (start, start_code_len))| {
            let nal_start = start + start_code_len;
            let nal_end = starts
                .get(index + 1)
                .map_or(payload.len(), |(next_start, _)| *next_start);
            (nal_start < nal_end).then_some(&payload[nal_start..nal_end])
        })
        .collect()
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn converts_length_prefixed_nals_to_annex_b() {
        let input = [0, 0, 0, 2, 0x65, 0xAA, 0, 0, 0, 1, 0x41];
        assert_eq!(
            nal_units_to_byte_stream(&input).unwrap(),
            [0, 0, 0, 1, 0x65, 0xAA, 0, 0, 0, 1, 0x41]
        );
    }

    #[test]
    fn rejects_truncated_nals() {
        assert!(nal_units_to_byte_stream(&[0, 0, 0, 4, 1]).is_err());
    }

    #[test]
    fn preserves_uxplay_queue_and_parser_order() {
        let h264 = video_pipeline_description(VideoCodec::H264);
        assert!(h264.contains("! queue ! h264parse ! decodebin"));
        assert!(h264.contains(
            "! videoscale ! queue leaky=upstream max-size-buffers=5 max-size-bytes=0 max-size-time=500000000 ! glupload ! qml6glsink"
        ));
        assert!(!h264.contains("leaky=downstream"));

        let h265 = video_pipeline_description(VideoCodec::H265);
        assert!(h265.contains("! queue ! h265parse ! decodebin"));
    }

    #[test]
    fn preserves_uxplay_audio_output_chain() {
        assert!(PCM_AUDIO_PIPELINE.contains(
            "! queue ! audioconvert ! audioresample ! volume name=master_volume ! level ! autoaudiosink sync=false"
        ));
        assert!(!PCM_AUDIO_PIPELINE.contains("do-timestamp=true"));
        assert!(!PCM_AUDIO_PIPELINE.contains("leaky="));
        assert!(AAC_ELD_AUDIO_PIPELINE.contains("! queue ! avdec_aac ! audioconvert"));
    }

    #[test]
    fn accepts_annex_b_nals_without_converting_them_again() {
        let input = [0, 0, 0, 1, 0x65, 0xAA, 0, 0, 1, 0x41];
        assert_eq!(nal_units_to_byte_stream(&input).unwrap(), input);
    }

    #[test]
    fn accepts_annex_b_h264_parameter_sets() {
        let input = [0, 0, 0, 1, 0x67, 0x64, 0, 0, 0, 1, 0x68, 0xEE];
        assert_eq!(nal_units_to_byte_stream(&input).unwrap(), input);
    }

    #[test]
    fn rejects_nals_with_forbidden_bit_set() {
        assert!(nal_units_to_byte_stream(&[0, 0, 0, 1, 0x80]).is_err());
    }
}
