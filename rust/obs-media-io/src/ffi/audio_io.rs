//! The C ABI of `libobs/media-io/audio-io.h`: `audio_t` is the opaque
//! `struct audio_output` — here a boxed [`crate::audio_io::AudioOutput`] seen
//! as `*mut c_void` — and the ten `EXPORT`ed functions wrap it.

use core::ffi::{c_char, c_int, c_void};

use crate::audio_io::{self, AudioOutput};
use crate::video_io::MAX_AV_PLANES;

pub const AUDIO_OUTPUT_SUCCESS: c_int = 0;
pub const AUDIO_OUTPUT_INVALIDPARAM: c_int = -1;
pub const AUDIO_OUTPUT_FAIL: c_int = -2;

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
    pub data: [*mut f32; audio_io::MAX_AUDIO_CHANNELS],
}

/// Mirrors `audio_input_callback_t`.
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
#[derive(Debug, Clone, Copy)]
pub struct audio_convert_info {
    pub samples_per_sec: u32,
    pub format: c_int,
    pub speakers: c_int,
    pub allow_clipping: bool,
}

/// Mirrors `struct resample_info` (`media-io/audio-resampler.h`).
#[repr(C)]
#[allow(non_camel_case_types)]
#[derive(Debug, Clone, Copy)]
pub struct resample_info {
    pub samples_per_sec: u32,
    pub format: c_int,
    pub speakers: c_int,
}

/// Mirrors `audio_output_callback_t`.
#[allow(non_camel_case_types)]
pub type audio_output_callback_t =
    Option<unsafe extern "C" fn(param: *mut c_void, mix_idx: usize, data: *mut audio_data)>;

/// `audio_output_open`. On `AUDIO_OUTPUT_INVALIDPARAM`/`AUDIO_OUTPUT_FAIL`
/// `*audio` is left untouched, like the C.
///
/// # Safety
/// `audio` must point to writable `audio_t` storage and `info` to a valid
/// `struct audio_output_info` for the duration of the call; the returned
/// handle owns a thread that keeps calling `info.input_callback` until
/// `audio_output_close`.#[cfg(unix)]
pub use unix::*;

#[cfg(unix)]
mod unix {
    use super::*;

    #[unsafe(no_mangle)]
    pub unsafe extern "C" fn audio_output_open(
        audio: *mut *mut c_void,
        info: *mut audio_output_info,
    ) -> c_int {
        // SAFETY: caller provides a valid info pointer per the C contract (a
        // NULL info faults in the C and here alike).
        match AudioOutput::open(unsafe { &*info }) {
            Ok(out) => {
                // SAFETY: caller provides a valid out-pointer slot.
                unsafe { *audio = Box::into_raw(out) as *mut c_void };
                AUDIO_OUTPUT_SUCCESS
            }
            Err(code) => code,
        }
    }

    /// `audio_output_close`.
    ///
    /// # Safety
    /// `audio` must be a handle from `audio_output_open` not yet closed, or NULL.
    #[unsafe(no_mangle)]
    pub unsafe extern "C" fn audio_output_close(audio: *mut c_void) {
        if audio.is_null() {
            return;
        }
        // SAFETY: ownership returns to the box once; Drop runs the close sequence.
        unsafe { drop(Box::from_raw(audio as *mut AudioOutput)) };
    }

    /// `audio_output_connect`.
    ///
    /// # Safety
    /// `audio` must be a live handle; `conversion`, when non-NULL, must point to
    /// a valid `struct audio_convert_info`.
    #[unsafe(no_mangle)]
    pub unsafe extern "C" fn audio_output_connect(
        audio: *mut c_void,
        mix_idx: usize,
        conversion: *const audio_convert_info,
        callback: audio_output_callback_t,
        param: *mut c_void,
    ) -> bool {
        if audio.is_null() {
            return false;
        }
        // SAFETY: live handle; `conversion` is valid per the C contract (NULL ->
        // None). The lock inside `connect` serializes the list; `&self` — no
        // exclusive reference is ever created on a live handle.
        let conversion = unsafe { conversion.as_ref() };
        unsafe { (&*(audio as *const AudioOutput)).connect(mix_idx, conversion, callback, param) }
    }

    /// `audio_output_disconnect`.
    ///
    /// # Safety
    /// `audio` must be a live handle or NULL.
    #[unsafe(no_mangle)]
    pub unsafe extern "C" fn audio_output_disconnect(
        audio: *mut c_void,
        mix_idx: usize,
        callback: audio_output_callback_t,
        param: *mut c_void,
    ) {
        if audio.is_null() {
            return;
        }
        // SAFETY: live handle; the lock inside `disconnect` serializes the list.
        unsafe { (&*(audio as *const AudioOutput)).disconnect(mix_idx, callback, param) }
    }

    /// `audio_output_get_info`.
    ///
    /// # Safety
    /// `audio` must be a live handle or NULL.
    #[unsafe(no_mangle)]
    pub unsafe extern "C" fn audio_output_get_info(audio: *const c_void) -> *const audio_output_info {
        if audio.is_null() {
            return core::ptr::null();
        }
        // SAFETY: live handle; `info` lives inside the shared state (the C
        // returns the same pointer).
        unsafe { &(*(audio as *const AudioOutput)).shared.info as *const audio_output_info }
    }

    /// `audio_output_active`.
    ///
    /// # Safety
    /// `audio` must be a live handle or NULL.
    #[unsafe(no_mangle)]
    pub unsafe extern "C" fn audio_output_active(audio: *const c_void) -> bool {
        if audio.is_null() {
            return false;
        }
        // SAFETY: live handle.
        unsafe { (&*(audio as *const AudioOutput)).active() }
    }

    /// `audio_output_get_block_size`. The C dereferences `audio` unconditionally;
    /// a NULL handle faults the same way here.
    ///
    /// # Safety
    /// `audio` must be a live handle.
    #[unsafe(no_mangle)]
    pub unsafe extern "C" fn audio_output_get_block_size(audio: *const c_void) -> usize {
        // SAFETY: C pins a NULL deref; reading a field off the handle preserves it.
        unsafe { (*(audio as *const AudioOutput)).shared.block_size }
    }

    /// `audio_output_get_planes`.
    ///
    /// # Safety
    /// `audio` must be a live handle.
    #[unsafe(no_mangle)]
    pub unsafe extern "C" fn audio_output_get_planes(audio: *const c_void) -> usize {
        // SAFETY: see get_block_size.
        unsafe { (*(audio as *const AudioOutput)).shared.planes }
    }

    /// `audio_output_get_channels`.
    ///
    /// # Safety
    /// `audio` must be a live handle.
    #[unsafe(no_mangle)]
    pub unsafe extern "C" fn audio_output_get_channels(audio: *const c_void) -> usize {
        // SAFETY: see get_block_size.
        unsafe { (*(audio as *const AudioOutput)).shared.channels }
    }

    /// `audio_output_get_sample_rate`.
    ///
    /// # Safety
    /// `audio` must be a live handle.
    #[unsafe(no_mangle)]
    pub unsafe extern "C" fn audio_output_get_sample_rate(audio: *const c_void) -> u32 {
        // SAFETY: see get_block_size.
        unsafe { (*(audio as *const AudioOutput)).shared.info.samples_per_sec }
    }
}
