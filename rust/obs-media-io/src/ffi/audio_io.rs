//! The structs of `libobs/media-io/audio-io.h` at the C boundary, and its
//! `static inline` helpers for enum values as they cross it.
//!
//! The helpers are compiled into every C caller, so nothing is exported or
//! swapped here. The functions below are for Rust shims that receive the
//! enums as `c_int`s: they convert, call the safe core in
//! [`crate::audio_io`], and give a value outside its enum the result the C
//! gives it. No C case matches such a value, so it behaves like the
//! enum's `UNKNOWN`: a layout outside `enum speaker_layout` (7 included)
//! has 0 channels, and a format outside `enum audio_format` has 0 bytes per
//! channel and is not planar.

use core::ffi::{c_char, c_int, c_void};

use crate::audio_io::{
    self, AudioFormat, MAX_AUDIO_CHANNELS, SpeakerLayout, audio_format_from_c,
    speaker_layout_from_c,
};
use crate::video_io::MAX_AV_PLANES;

/// Mirrors `struct audio_data`.
#[repr(C)]
#[allow(non_camel_case_types)]
#[derive(Debug, Clone, Copy)]
pub struct audio_data {
    pub data: [*mut u8; MAX_AV_PLANES],
    pub frames: u32,
    pub timestamp: u64,
}

/// Mirrors `struct audio_output_data`.
#[repr(C)]
#[allow(non_camel_case_types)]
#[derive(Debug, Clone, Copy)]
pub struct audio_output_data {
    pub data: [*mut f32; MAX_AUDIO_CHANNELS],
}

/// Mirrors `audio_input_callback_t`. `None` is a NULL callback.
#[allow(non_camel_case_types)]
pub type audio_input_callback_t = Option<
    unsafe extern "C" fn(
        param: *mut c_void,
        start_ts: u64,
        end_ts: u64,
        new_ts: *mut u64,
        active_mixers: u32,
        mixes: *mut audio_output_data,
    ) -> bool,
>;

/// Mirrors `struct audio_output_info`. The enums are `c_int`s.
#[repr(C)]
#[allow(non_camel_case_types)]
#[derive(Debug, Clone, Copy)]
pub struct audio_output_info {
    pub name: *const c_char,

    pub samples_per_sec: u32,
    pub format: c_int,
    pub speakers: c_int,

    pub input_callback: audio_input_callback_t,
    pub input_param: *mut c_void,
}

/// Mirrors `struct audio_convert_info`. The enums are `c_int`s.
#[repr(C)]
#[allow(non_camel_case_types)]
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct audio_convert_info {
    pub samples_per_sec: u32,
    pub format: c_int,
    pub speakers: c_int,
    pub allow_clipping: bool,
}

fn to_format(format: c_int) -> AudioFormat {
    audio_format_from_c(format).unwrap_or(AudioFormat::Unknown)
}

fn to_speakers(speakers: c_int) -> SpeakerLayout {
    speaker_layout_from_c(speakers).unwrap_or(SpeakerLayout::Unknown)
}

/// `get_audio_channels` for a C `enum speaker_layout` value.
pub fn get_audio_channels(speakers: c_int) -> u32 {
    audio_io::audio_channels(to_speakers(speakers))
}

/// `get_audio_bytes_per_channel` for a C `enum audio_format` value.
pub fn get_audio_bytes_per_channel(format: c_int) -> usize {
    audio_io::audio_bytes_per_channel(to_format(format))
}

/// `is_audio_planar` for a C `enum audio_format` value.
pub fn is_audio_planar(format: c_int) -> bool {
    audio_io::is_audio_planar(to_format(format))
}

/// `get_audio_planes` for C `enum audio_format` and `enum speaker_layout`
/// values.
pub fn get_audio_planes(format: c_int, speakers: c_int) -> usize {
    audio_io::audio_planes(to_format(format), to_speakers(speakers))
}

/// `get_audio_size` for C `enum audio_format` and `enum speaker_layout`
/// values.
pub fn get_audio_size(format: c_int, speakers: c_int, frames: u32) -> usize {
    audio_io::audio_size(to_format(format), to_speakers(speakers), frames)
}

/// `get_total_audio_size` for C `enum audio_format` and
/// `enum speaker_layout` values.
pub fn get_total_audio_size(format: c_int, speakers: c_int, frames: u32) -> usize {
    audio_io::total_audio_size(to_format(format), to_speakers(speakers), frames)
}
