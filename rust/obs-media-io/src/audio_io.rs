//! Types and `static inline` helpers from `libobs/media-io/audio-io.h`.

use core::ffi::c_int;

use obs_util::util_uint64::mul_div64;

/// `MAX_AUDIO_MIXES`: the number of audio mixes (tracks).
pub const MAX_AUDIO_MIXES: usize = 6;

/// `MAX_AUDIO_CHANNELS`: the most channels a layout has.
pub const MAX_AUDIO_CHANNELS: usize = 8;

/// `MAX_DEVICE_INPUT_CHANNELS`.
pub const MAX_DEVICE_INPUT_CHANNELS: usize = 64;

/// `AUDIO_OUTPUT_FRAMES`: the frames in one audio output tick.
pub const AUDIO_OUTPUT_FRAMES: usize = 1024;

/// `TOTAL_AUDIO_SIZE`: the bytes of every mix at [`MAX_AUDIO_CHANNELS`]
/// channels of [`AUDIO_OUTPUT_FRAMES`] `float` frames.
pub const TOTAL_AUDIO_SIZE: usize =
    MAX_AUDIO_MIXES * MAX_AUDIO_CHANNELS * AUDIO_OUTPUT_FRAMES * size_of::<f32>();

/// `enum audio_format`, in header order. The discriminants are the
/// header's values; the C ABI passes the enum as a `c_int`, converted with
/// [`audio_format_to_c`].
#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash)]
pub enum AudioFormat {
    Unknown = 0,

    U8Bit = 1,
    Bit16 = 2,
    Bit32 = 3,
    Float = 4,

    U8BitPlanar = 5,
    Bit16Planar = 6,
    Bit32Planar = 7,
    FloatPlanar = 8,
}

impl AudioFormat {
    /// Every variant, in header order.
    pub const ALL: [Self; 9] = [
        Self::Unknown,
        Self::U8Bit,
        Self::Bit16,
        Self::Bit32,
        Self::Float,
        Self::U8BitPlanar,
        Self::Bit16Planar,
        Self::Bit32Planar,
        Self::FloatPlanar,
    ];
}

/// The C value of `enum audio_format` for `format`.
pub fn audio_format_to_c(format: AudioFormat) -> c_int {
    format as c_int
}

/// The format whose C value is `value`, or `None` for a value outside
/// `enum audio_format`.
pub fn audio_format_from_c(value: c_int) -> Option<AudioFormat> {
    AudioFormat::ALL
        .into_iter()
        .find(|&format| audio_format_to_c(format) == value)
}

/// `enum speaker_layout`, in header order. The discriminants are the
/// header's values, which skip 7; the C ABI passes the enum as a `c_int`.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash)]
pub enum SpeakerLayout {
    /// Unknown setting, fallback is stereo.
    Unknown = 0,
    /// MONO
    Mono = 1,
    /// FL, FR
    Stereo = 2,
    /// FL, FR, LFE
    TwoPointOne = 3,
    /// FL, FR, FC, RC
    FourPointZero = 4,
    /// FL, FR, FC, LFE, RC
    FourPointOne = 5,
    /// FL, FR, FC, LFE, RL, RR
    FivePointOne = 6,
    /// FL, FR, FC, LFE, RL, RR, SL, SR
    SevenPointOne = 8,
}

impl SpeakerLayout {
    /// Every variant, in header order.
    pub const ALL: [Self; 8] = [
        Self::Unknown,
        Self::Mono,
        Self::Stereo,
        Self::TwoPointOne,
        Self::FourPointZero,
        Self::FourPointOne,
        Self::FivePointOne,
        Self::SevenPointOne,
    ];
}

/// The C value of `enum speaker_layout` for `speakers`.
pub fn speaker_layout_to_c(speakers: SpeakerLayout) -> c_int {
    speakers as c_int
}

/// The layout whose C value is `value`, or `None` for a value outside
/// `enum speaker_layout`.
pub fn speaker_layout_from_c(value: c_int) -> Option<SpeakerLayout> {
    SpeakerLayout::ALL
        .into_iter()
        .find(|&speakers| speaker_layout_to_c(speakers) == value)
}

/// `get_audio_channels`: the channels of `speakers`, 0 for
/// [`SpeakerLayout::Unknown`].
pub fn audio_channels(speakers: SpeakerLayout) -> u32 {
    match speakers {
        SpeakerLayout::Mono => 1,
        SpeakerLayout::Stereo => 2,
        SpeakerLayout::TwoPointOne => 3,
        SpeakerLayout::FourPointZero => 4,
        SpeakerLayout::FourPointOne => 5,
        SpeakerLayout::FivePointOne => 6,
        SpeakerLayout::SevenPointOne => 8,
        SpeakerLayout::Unknown => 0,
    }
}

/// `get_audio_bytes_per_channel`: the bytes of one sample of `format`, 0
/// for [`AudioFormat::Unknown`].
pub fn audio_bytes_per_channel(format: AudioFormat) -> usize {
    match format {
        AudioFormat::U8Bit | AudioFormat::U8BitPlanar => 1,

        AudioFormat::Bit16 | AudioFormat::Bit16Planar => 2,

        AudioFormat::Float
        | AudioFormat::FloatPlanar
        | AudioFormat::Bit32
        | AudioFormat::Bit32Planar => 4,

        AudioFormat::Unknown => 0,
    }
}

/// `is_audio_planar`: whether `format` keeps each channel in its own
/// plane.
pub fn is_audio_planar(format: AudioFormat) -> bool {
    match format {
        AudioFormat::U8Bit | AudioFormat::Bit16 | AudioFormat::Bit32 | AudioFormat::Float => false,

        AudioFormat::U8BitPlanar
        | AudioFormat::FloatPlanar
        | AudioFormat::Bit16Planar
        | AudioFormat::Bit32Planar => true,

        AudioFormat::Unknown => false,
    }
}

/// The channels of `speakers` as a `usize`, as the C's `uint32_t` converts
/// to `size_t`.
fn channels(speakers: SpeakerLayout) -> usize {
    // At most 8, so this never fails.
    usize::try_from(audio_channels(speakers)).unwrap()
}

/// `frames` as a `usize`, as the C's `uint32_t` converts to `size_t`.
fn frames_usize(frames: u32) -> usize {
    usize::try_from(frames).expect("size_t holds uint32_t")
}

/// `get_audio_planes`: one plane per channel for a planar `format`, else
/// one.
pub fn audio_planes(format: AudioFormat, speakers: SpeakerLayout) -> usize {
    if is_audio_planar(format) {
        channels(speakers)
    } else {
        1
    }
}

/// `get_audio_size`: the bytes of `frames` frames in one plane. Wraps like
/// the C's `size_t` arithmetic.
pub fn audio_size(format: AudioFormat, speakers: SpeakerLayout, frames: u32) -> usize {
    let channels = if is_audio_planar(format) {
        1
    } else {
        channels(speakers)
    };
    channels
        .wrapping_mul(audio_bytes_per_channel(format))
        .wrapping_mul(frames_usize(frames))
}

/// `get_total_audio_size`: the bytes of `frames` frames across all planes.
/// Wraps like the C's `size_t` arithmetic.
pub fn total_audio_size(format: AudioFormat, speakers: SpeakerLayout, frames: u32) -> usize {
    channels(speakers)
        .wrapping_mul(audio_bytes_per_channel(format))
        .wrapping_mul(frames_usize(frames))
}

const NS_PER_SEC: u64 = 1_000_000_000;

/// `sample_rate` as a `u64`, as the C's `size_t` converts to `uint64_t`.
fn rate_u64(sample_rate: usize) -> u64 {
    u64::try_from(sample_rate).expect("uint64_t holds size_t")
}

/// `audio_frames_to_ns`: the duration of `frames` frames at `sample_rate`,
/// in nanoseconds, truncated.
///
/// # Panics
///
/// Where [`obs_util::util_uint64::mul_div64`] does: if `sample_rate` is 0,
/// and on MSVC x64 if the result does not fit in 64 bits.
pub fn audio_frames_to_ns(sample_rate: usize, frames: u64) -> u64 {
    mul_div64(frames, NS_PER_SEC, rate_u64(sample_rate))
}

/// `ns_to_audio_frames`: the frames in `ns` nanoseconds at `sample_rate`,
/// truncated.
///
/// # Panics
///
/// Where [`obs_util::util_uint64::mul_div64`] does: on MSVC x64 if the
/// result does not fit in 64 bits.
pub fn ns_to_audio_frames(sample_rate: usize, ns: u64) -> u64 {
    mul_div64(ns, rate_u64(sample_rate), NS_PER_SEC)
}
