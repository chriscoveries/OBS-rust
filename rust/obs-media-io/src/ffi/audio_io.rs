//! The structs of `libobs/media-io/audio-io.h` at the C boundary, and its
//! `static inline` helpers for enum values as they cross it.
//!
//! The helpers are compiled into every C caller, so nothing is exported or
//! swapped here. The ten `audio_output_*` `EXPORT`ed functions live in
//! [`unix`] (`#[cfg(unix)]`, re-exported flat): `audio_t` is the opaque
//! `struct audio_output` — a boxed [`crate::audio_io::AudioOutput`] seen as
//! `*mut c_void`. The functions below are for Rust shims that receive the
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

pub const AUDIO_OUTPUT_SUCCESS: c_int = 0;
pub const AUDIO_OUTPUT_INVALIDPARAM: c_int = -1;
pub const AUDIO_OUTPUT_FAIL: c_int = -2;

// Engine shims. Compiled where `with_audio_thread_port!` expands.
crate::platform::with_audio_thread_port! {
    pub use engine::*;
    mod engine {
    use super::*;
    use crate::audio_io::AudioOutput;

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
    /// `audio_output_close`. `info.name` must stay a valid NUL-terminated
    /// string for the handle's whole life (the shallow info copy keeps the
    /// pointer and the worker formats it), and `info.input_param` must stay
    /// valid until close.
    #[unsafe(no_mangle)]
    pub unsafe extern "C" fn audio_output_open(
        audio: *mut *mut c_void,
        info: *mut audio_output_info,
    ) -> c_int {
        // SAFETY: caller provides a valid info pointer per the C contract (a
        // NULL info faults in the C and here alike); `info.name`/`input_param`
        // validity is the caller's per the doc contract above.
        match unsafe { AudioOutput::open(&*info) } {
            Ok(out) => {
                // SAFETY: caller provides a valid out-pointer slot.
                unsafe { *audio = Box::into_raw(out) as *mut c_void };
                AUDIO_OUTPUT_SUCCESS
            }
            Err(code) => code,
        }
    }

    /// `audio_output_close`. Stops and joins the worker before freeing any
    /// input state, so input callbacks are never invoked after close
    /// returns. The join is a full barrier: callbacks run only on the
    /// worker thread, so none can execute after the join. An in-flight
    /// callback may still reach back into the handle (e.g. disconnect)
    /// *during* the join — safe because no `&mut AudioOutput` exists until
    /// the worker is dead, matching the C, which has no aliasing rule.
    ///
    /// # Safety
    /// `audio` must be a handle from `audio_output_open` not yet closed, or
    /// NULL. Every context passed to `audio_output_connect` on this handle
    /// must survive until close returns. Must not be called from inside an
    /// input or output callback — re-entrant close joins the calling
    /// worker thread, UB here exactly as `pthread_join` on the calling
    /// thread is UB in the C.
    #[unsafe(no_mangle)]
    pub unsafe extern "C" fn audio_output_close(audio: *mut c_void) {
        if audio.is_null() {
            return;
        }
        let out = audio as *mut AudioOutput;
        // SAFETY: live handle. Stop+join happens through the raw pointer
        // only — forming the Box's `&mut` here would alias the `&` an
        // in-flight callback can still hold into `shared`.
        unsafe { AudioOutput::stop_and_join(out) };
        // SAFETY: the worker is dead and every callback has returned, so no
        // foreign `&` can appear again; the owning Box is sound to
        // reconstruct. Drop's stop/join is a no-op (`thread` is None); it
        // still frees the mix resamplers.
        unsafe { drop(Box::from_raw(out)) };
    }

    /// `audio_output_connect`.
    ///
    /// # Safety
    /// `audio` must be a live handle; `conversion`, when non-NULL, must point
    /// to a valid `struct audio_convert_info`. `param` must stay valid until
    /// a matching `audio_output_disconnect` or `audio_output_close` — and
    /// outlive any in-flight callback return. `callback` is invoked on the
    /// audio worker thread with the recursive input lock held; it may
    /// re-enter `audio_output_connect`/`disconnect`/`active` on the same
    /// thread only (re-entry from another thread blocks on the lock) —
    /// never `audio_output_close`: joining the calling worker thread is
    /// UB, as it is in the C.
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
    /// `audio` must be a live handle or NULL. Disconnecting frees the
    /// input's resampler state; the `param` context itself must still
    /// outlive any in-flight dispatch of `callback` on the worker thread.
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
    pub unsafe extern "C" fn audio_output_get_info(
        audio: *const c_void,
    ) -> *const audio_output_info {
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
}
