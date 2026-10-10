//! RED-stage stub of the `libobs/media-io/audio-io.c` port: every entry
//! point exists so the C ABI shim links and the Tier-1 tests compile, but
//! `open` always refuses — the tests are expected to fail until the real
//! implementation lands.

use core::ffi::{c_int, c_void};

use crate::ffi::audio_io::{
    audio_convert_info, audio_output_callback_t, audio_output_info, AUDIO_OUTPUT_FAIL,
};

pub const MAX_AUDIO_MIXES: usize = 6;
pub const MAX_AUDIO_CHANNELS: usize = 8;
pub const AUDIO_OUTPUT_FRAMES: usize = 1024;

/// `struct audio_output` (stub).
pub struct AudioOutput {
    pub(crate) info: audio_output_info,
    pub(crate) block_size: usize,
    pub(crate) channels: usize,
    pub(crate) planes: usize,
}

impl AudioOutput {
    /// `audio_output_open` (stub): always fails, `*audio` untouched.
    pub fn open(_info: *mut audio_output_info) -> Result<Box<Self>, c_int> {
        Err(AUDIO_OUTPUT_FAIL)
    }

    /// `audio_output_connect` (stub).
    pub fn connect(
        &mut self,
        _mix_idx: usize,
        _conversion: *const audio_convert_info,
        _callback: audio_output_callback_t,
        _param: *mut c_void,
    ) -> bool {
        false
    }

    /// `audio_output_disconnect` (stub).
    pub fn disconnect(
        &mut self,
        _mix_idx: usize,
        _callback: audio_output_callback_t,
        _param: *mut c_void,
    ) {
    }

    /// `audio_output_active` (stub).
    pub fn active(&self) -> bool {
        false
    }
}
