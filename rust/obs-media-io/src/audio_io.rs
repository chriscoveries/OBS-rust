//! Types and `static inline` helpers from `libobs/media-io/audio-io.h`,
//! plus the `audio-io.c` port in [`port`] (`#[cfg(unix)]`, re-exported flat).

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

#[cfg(unix)]
mod port {
//! `libobs/media-io/audio-io.c`: the audio output object, its per-mix input
//! lists, and the audio thread that drives them — in safe Rust over the same
//! libobs platform calls the C makes (`os_*` timing, `audio_resampler_*` and
//! the profiler hooks resolve when the staticlib links into libobs, and under
//! `cargo test` into the oracle's real POSIX threading build plus its stubs).
//!
//! Layout notes carried over from the C: for a non-planar format the input
//! callback gets one `data` pointer per output and writes interleaved frames
//! across all channel rows of `mix.buffer`, so the clamp pass works on
//! `frames * block_size` bytes starting at a plane row — it deliberately
//! walks past the row boundary into the following rows. The C does the same
//! arithmetic on `float *`.
//!
//! Ownership (fork PR #35 review): `AudioOutput` is the owner — it holds the
//! `JoinHandle` — and `Shared` is the state the audio thread and concurrent
//! `connect`/`disconnect` callers reach at the same time. No `&mut
//! AudioOutput` and no `&mut Shared` is ever created after `open`; the worker
//! gets a `&Shared` for its whole life and every mutation goes through the
//! recursive `input_mutex` plus `UnsafeCell`, scoped to a single statement.
//! `Shared` is heap-allocated before the pthread mutex is initialized, so the
//! mutex is created at its final address (POSIX forbids moving an
//! initialized mutex).
//!
//! Unsafe-location exception (policy §unsafe): the audio thread shares state
//! with FFI callers, so `Shared` carries one `UnsafeCell` behind the lock —
//! the C does the same with `pthread_mutex_*` on the same fields. All unsafe
//! stays in single-statement accessors with SAFETY comments. The raw
//! C-shaped boundary — `open`/`open_no_worker`/`connect`/`tick_for_test` —
//! is `unsafe` with documented name/context/thread contracts; `disconnect`,
//! `active` and drop retain nothing new and stay safe.
//!
//! Documented deviations from the C (all unreachable-but-defined inputs):
//! - `input_callback == NULL`: the C calls the pointer unconditionally and
//!   faults; the port treats the tick as failed and returns, like a callback
//!   that returned false.
//! - Output `callback == NULL`: the C accepts the connect then faults on the
//!   next dispatch; the port accepts the connect (same return value) but
//!   skips the dead call, keeping the input active.
//! - Mid-dispatch removals that shrink the list below the loop's current
//!   index: the C reads the stale darray tail (freed-capacity bytes) and
//!   invokes it; the port skips — no defined C equivalent to reproduce.
//! - Unix-first: gated `#[cfg(unix)]` at the module level — libc exposes no
//!   pthread API on Windows, so Windows keeps `media-io/audio-io.c` (its
//!   exports stay marked C-side); the oracle's POSIX timing is gated the
//!   same way. A Windows backend is separate work.
//!
use super::*;

use core::ffi::{c_char, c_int, c_void};
use core::ptr;
use std::cell::UnsafeCell;
use std::sync::atomic::{AtomicBool, Ordering};
use std::thread::JoinHandle;

use crate::ffi::audio_io::{
    AUDIO_OUTPUT_FAIL, AUDIO_OUTPUT_INVALIDPARAM, audio_data, audio_input_callback_t,
    audio_output_callback_t, audio_output_data, audio_output_info,
    get_audio_bytes_per_channel, get_audio_channels, is_audio_planar, resample_info,
};
use crate::video_io::MAX_AV_PLANES;
use obs_util::util_uint64::mul_div64;

const LOG_ERROR: c_int = 100;

unsafe extern "C" {
    fn os_gettime_ns() -> u64;
    fn os_sleepto_ns_fast(time_target: u64) -> bool;
    fn os_set_thread_name(name: *const c_char);
    fn obs_get_profiler_name_store() -> *mut c_void;
    fn profile_store_name(store: *mut c_void, format: *const c_char, ...) -> *const c_char;
    fn profile_start(name: *const c_char);
    fn profile_end(name: *const c_char);
    fn profile_reenable_thread();
    fn blog(level: c_int, format: *const c_char, ...);
    fn audio_resampler_create(dst: *const resample_info, src: *const resample_info) -> *mut c_void;
    fn audio_resampler_destroy(resampler: *mut c_void);
    fn audio_resampler_resample(
        resampler: *mut c_void,
        output: *mut *mut u8,
        out_frames: *mut u32,
        ts_offset: *mut u64,
        input: *const *const u8,
        in_frames: u32,
    ) -> bool;
}


/// `pthread_mutex_init_recursive`: `audio_output_connect`/`disconnect` lock
/// `input_mutex` and the C comments in `do_audio_output` let an output
/// callback disconnect or connect again from inside the audio thread, which
/// only works because the mutex is recursive.
///
/// The `pthread_mutex_t` lives in the `Shared` box: `init()` runs only after
/// boxing, at the mutex's final address — POSIX forbids moving an
/// initialized mutex, so the previous stack-`new()`-then-move was unsound.
struct InputMutex {
    inner: UnsafeCell<libc::pthread_mutex_t>,
    initialized: bool,
}

// Same-thread re-lock is the point; cross-thread sharing is serialized by
// the mutex itself.
unsafe impl Send for InputMutex {}
unsafe impl Sync for InputMutex {}

impl InputMutex {
    /// Zeroed storage; `init()` must run once at the object's final address
    /// before any `lock`/`unlock`.
    fn uninit() -> Self {
        // SAFETY: pthread_mutex_t is plain POD; zeroed bytes are a valid
        // pre-init representation.
        Self {
            inner: UnsafeCell::new(unsafe { std::mem::zeroed() }),
            initialized: false,
        }
    }

    /// `pthread_mutex_init_recursive` at the object's final address.
    /// # Safety
    /// Call exactly once, before the mutex is shared.
    unsafe fn init(&mut self) -> Result<(), c_int> {
        unsafe {
            let mut attr: libc::pthread_mutexattr_t = std::mem::zeroed();
            if libc::pthread_mutexattr_init(&mut attr) != 0 {
                return Err(-1);
            }
            libc::pthread_mutexattr_settype(&mut attr, libc::PTHREAD_MUTEX_RECURSIVE);
            let rc = libc::pthread_mutex_init(self.inner.get(), &attr);
            libc::pthread_mutexattr_destroy(&mut attr);
            if rc != 0 {
                return Err(-1);
            }
            self.initialized = true;
            Ok(())
        }
    }

    #[inline]
    fn lock(&self) {
        // SAFETY: initialized in open before the mutex is shared.
        unsafe { libc::pthread_mutex_lock(self.inner.get()) };
    }

    #[inline]
    fn unlock(&self) {
        // SAFETY: see lock.
        unsafe { libc::pthread_mutex_unlock(self.inner.get()) };
    }
}

impl Drop for InputMutex {
    fn drop(&mut self) {
        if self.initialized {
            // SAFETY: initialized in open; only destroyed after the audio
            // thread joined (nothing can hold the lock).
            unsafe { libc::pthread_mutex_destroy(self.inner.get()) };
        }
    }
}

/// `struct audio_input`.
struct AudioInput {
    conversion: crate::ffi::audio_io::audio_convert_info,
    resampler: *mut c_void,
    callback: audio_output_callback_t,
    param: *mut c_void,
}

/// `struct audio_mix`, with the two `[MAX_AUDIO_CHANNELS][AUDIO_OUTPUT_FRAMES]`
/// buffers stored flat like the C's contiguous rows. Each field is its own
/// `UnsafeCell`: the input registry is reached only under `input_mutex`,
/// while the buffers are touched outside the lock (the tick's fill/clamp —
/// only the thread running the tick ever touches them), so a `&mut` on the
/// registry can never overlap a `&mut` on a buffer through one shared cell.
struct AudioMix {
    inputs: UnsafeCell<Vec<AudioInput>>,
    buffer: UnsafeCell<[f32; MAX_AUDIO_CHANNELS * AUDIO_OUTPUT_FRAMES]>,
    buffer_unclamped: UnsafeCell<[f32; MAX_AUDIO_CHANNELS * AUDIO_OUTPUT_FRAMES]>,
}

/// The state the audio thread and concurrent FFI callers share. Everything
/// except `mixes` and `stop` is immutable after `open` (plain reads need no
/// lock); `inputs` is reached only under `input_mutex`. The box never moves,
/// so raw pointers into `buffer`/`buffer_unclamped` stay valid across
/// re-entrant `connect`/`disconnect` calls (those only touch `inputs`).
///
/// Borrow discipline: every `&`/`&mut` into a mix field is created inside a
/// single expression and never held across a statement that can run a
/// callback or release the lock — so no borrow is ever live while a
/// re-entrant call creates another one.
pub(crate) struct Shared {
    pub(crate) info: audio_output_info,
    pub(crate) block_size: usize,
    pub(crate) channels: usize,
    pub(crate) planes: usize,

    input_cb: audio_input_callback_t,
    input_param: *mut c_void,

    stop: AtomicBool,
    input_mutex: InputMutex,
    mixes: [AudioMix; MAX_AUDIO_MIXES],
}

// Raw C pointers are passed across the API (param, callback, resampler); the
// audio thread and callers share `Shared` exactly like the C shares
// `struct audio_output`.
unsafe impl Send for Shared {}
unsafe impl Sync for Shared {}

struct SendShared(*const Shared);
unsafe impl Send for SendShared {}

impl SendShared {
    fn into_inner(self) -> *const Shared {
        self.0
    }
}

/// `struct audio_output` — the owner. `audio_t` handles point at this; the
/// join handle stays here (never inside `Shared`) so `drop` can stop and
/// join the worker before the shared box is torn down, without holding the
/// input lock.
pub struct AudioOutput {
    pub(crate) shared: Box<Shared>,
    thread: Option<JoinHandle<()>>,
    initialized: bool,
}

// FFI callers move handles across threads; all shared state sits behind
// `Shared`'s lock or its immutable fields.
unsafe impl Send for AudioOutput {}
unsafe impl Sync for AudioOutput {}

fn resample_audio_output(resampler: *mut c_void, data: &mut audio_data) -> bool {
    if resampler.is_null() {
        return true;
    }

    let mut output: [*mut u8; MAX_AV_PLANES] = [ptr::null_mut(); MAX_AV_PLANES];
    let mut frames: u32 = 0;
    let mut offset: u64 = 0;

    let success = unsafe {
        // SAFETY: `resampler` was returned by audio_resampler_create for this
        // input; output/frames/offset are valid out-params for the call.
        audio_resampler_resample(
            resampler,
            output.as_mut_ptr(),
            &mut frames,
            &mut offset,
            data.data.as_ptr() as *const *const u8,
            data.frames,
        )
    };

    data.data = output;
    data.frames = frames;
    data.timestamp = data.timestamp.wrapping_sub(offset);
    success
}

/// `audio_input_free`.
fn audio_input_free(input: &mut AudioInput) {
    // SAFETY: resampler is either NULL or a live audio_resampler_create
    // handle destroyed exactly once (disconnect erases, close frees all).
    unsafe { audio_resampler_destroy(input.resampler) };
}

impl Shared {
    /// One `do_audio_output` mix dispatch, C shape: lock the recursive mutex
    /// for the whole walk; the loop bound is fixed at entry
    /// (`for (i = inputs.num; i > 0; i--)`) and index `i-1` is re-read each
    /// iteration, so a callback that removes an earlier input re-invokes the
    /// element that shifts into the visited slot, and a callback that
    /// appends is not called this tick.
    fn do_audio_output(&self, mix_idx: usize, timestamp: u64, frames: u32) {
        let planes = self.planes;

        self.input_mutex.lock();

        let mut data = audio_data {
            data: [ptr::null_mut(); MAX_AV_PLANES],
            frames: 0,
            timestamp: 0,
        };

        let mix = &self.mixes[mix_idx];
        let mut i = {
            // SAFETY: the lock is held; no other accessor is live.
            unsafe { (*mix.inputs.get()).len() }
        };
        while i > 0 {
            i -= 1;
            // Re-read index i into locals and end every list/element borrow
            // before the callback runs: the callback may re-enter
            // connect/disconnect under this same recursive lock.
            let Some((resampler, callback, param, buf_ptr)) = ({
                // SAFETY: the lock is held; this borrow ends before the
                // callback below.
                let inputs = unsafe { &mut *mix.inputs.get() };
                inputs.get(i).map(|input| {
                    let buf = if input.conversion.allow_clipping {
                        mix.buffer_unclamped.get()
                    } else {
                        mix.buffer.get()
                    };
                    (
                        input.resampler,
                        input.callback,
                        input.param,
                        buf.cast::<f32>(),
                    )
                })
            }) else {
                // More inputs were removed than the loop has left to visit;
                // the C would call into freed darray tail — skip (documented
                // deviation).
                continue;
            };

            for (p, slot) in data.data.iter_mut().enumerate().take(planes) {
                // SAFETY: buf_ptr points into the mix's fixed buffer rows,
                // stable for the Shared box's life.
                *slot = unsafe { buf_ptr.add(p * AUDIO_OUTPUT_FRAMES) } as *mut u8;
            }

            data.frames = frames;
            data.timestamp = timestamp;

            if resample_audio_output(resampler, &mut data)
                && let Some(callback) = callback
            {
                // Still under the recursive lock: re-entrant
                // connect/disconnect is legal, like the C's.
                unsafe { callback(param, mix_idx, &mut data) };
            }
        }

        self.input_mutex.unlock();
    }

    /// `clamp_audio_output`. `clamp_mixes` is the mask re-read under the
    /// lock right after the input callback — the C's post-callback
    /// `!mix->inputs.num` check, not the mask the callback received.
    fn clamp_audio_output(&self, bytes: usize, clamp_mixes: u32) {
        let float_size = bytes / core::mem::size_of::<f32>();

        for mix_idx in 0..MAX_AUDIO_MIXES {
            if clamp_mixes & (1 << mix_idx) == 0 {
                continue;
            }

            for plane in 0..self.planes {
                let start = plane * AUDIO_OUTPUT_FRAMES;
                let end = start + float_size;
                // SAFETY: scoped borrows of two disjoint cells of this mix;
                // buffers are only touched by this thread's tick path, never
                // by connect/disconnect.
                let mix = &self.mixes[mix_idx];
                let unclamped = unsafe { &mut *mix.buffer_unclamped.get() };
                unclamped[start..end].copy_from_slice(&unsafe { &*mix.buffer.get() }[start..end]);
                for val in &mut unsafe { &mut *mix.buffer.get() }[start..end] {
                    let mut v = *val;
                    if v.is_nan() {
                        v = 0.0;
                    }
                    *val = v.clamp(-1.0, 1.0);
                }
            }
        }
    }

    /// `input_and_output`: one audio-thread tick.
    fn input_and_output(&self, audio_time: u64, prev_time: u64) {
        let bytes = AUDIO_OUTPUT_FRAMES * self.block_size;
        let mut data = [audio_output_data {
            data: [ptr::null_mut(); MAX_AUDIO_CHANNELS],
        }; MAX_AUDIO_MIXES];
        let mut active_mixes: u32 = 0;
        let mut new_ts: u64 = 0;

        self.input_mutex.lock();
        for i in 0..MAX_AUDIO_MIXES {
            // SAFETY: the lock is held; this borrow ends each statement.
            if unsafe { !(*self.mixes[i].inputs.get()).is_empty() } {
                active_mixes |= 1 << i;
            }
        }
        self.input_mutex.unlock();

        for (mix_idx, out) in data.iter_mut().enumerate() {
            let buf_ptr = {
                let mix = &self.mixes[mix_idx];
                // SAFETY: buffer cell only — the registry is untouched here
                // and only this thread's tick ever touches buffers.
                let buf = unsafe { &mut *mix.buffer.get() };
                buf.fill(0.0);
                buf.as_mut_ptr()
            };
            for (i, slot) in out.data.iter_mut().enumerate().take(self.planes) {
                // SAFETY: fixed buffer rows, stable for the Shared box's life.
                *slot = unsafe { buf_ptr.add(i * AUDIO_OUTPUT_FRAMES) };
            }
        }

        // `input_cb == NULL`: the C calls it unconditionally and faults; the
        // port treats the tick as failed and returns (documented deviation).
        let Some(input_cb) = self.input_cb else {
            return;
        };
        let success = unsafe {
            // SAFETY: callback + param come verbatim from the caller's
            // audio_output_info; data points at `data` above for the call.
            input_cb(
                self.input_param,
                prev_time,
                audio_time,
                &mut new_ts,
                active_mixes,
                data.as_mut_ptr(),
            )
        };
        if !success {
            return;
        }

        // Clamp activity is re-read after the input callback, like the C's
        // `!mix->inputs.num` check: a mix that gained an input mid-callback
        // is clamped anyway. `active_mixes` stays the callback's mask.
        self.input_mutex.lock();
        let mut clamp_mixes: u32 = 0;
        for i in 0..MAX_AUDIO_MIXES {
            // SAFETY: the lock is held; this borrow ends each statement.
            if unsafe { !(*self.mixes[i].inputs.get()).is_empty() } {
                clamp_mixes |= 1 << i;
            }
        }
        self.input_mutex.unlock();

        self.clamp_audio_output(bytes, clamp_mixes);

        for i in 0..MAX_AUDIO_MIXES {
            self.do_audio_output(i, new_ts, AUDIO_OUTPUT_FRAMES as u32);
        }
    }
}

/// `audio_get_input_idx`: position of (callback, param) in a mix, or None
/// like the C's `DARRAY_INVALID`.
fn input_index(
    inputs: &[AudioInput],
    callback: audio_output_callback_t,
    param: *mut c_void,
) -> Option<usize> {
    inputs.iter().position(|i| {
        let same_fn = match (i.callback, callback) {
            (Some(a), Some(b)) => std::ptr::fn_addr_eq(a, b),
            (None, None) => true,
            _ => false,
        };
        same_fn && i.param == param
    })
}

/// `audio_input_init`: create a resampler only when the conversion differs
/// from the output format/rate/layout.
fn audio_input_init(input: &mut AudioInput, info: &audio_output_info) -> bool {
    if input.conversion.format != info.format
        || input.conversion.samples_per_sec != info.samples_per_sec
        || input.conversion.speakers != info.speakers
    {
        let from = resample_info {
            format: info.format,
            samples_per_sec: info.samples_per_sec,
            speakers: info.speakers,
        };
        let to = resample_info {
            format: input.conversion.format,
            samples_per_sec: input.conversion.samples_per_sec,
            speakers: input.conversion.speakers,
        };

        // SAFETY: both info structs are valid for the call.
        input.resampler = unsafe { audio_resampler_create(&to, &from) };
        if input.resampler.is_null() {
            unsafe {
                blog(
                    LOG_ERROR,
                    c"audio_input_init: Failed to create resampler".as_ptr(),
                )
            };
            return false;
        }
    } else {
        input.resampler = ptr::null_mut();
    }

    true
}

impl AudioOutput {
    /// `valid_audio_params`.
    fn valid_audio_params(info: &audio_output_info) -> bool {
        info.format != 0 && !info.name.is_null() && info.samples_per_sec > 0 && info.speakers > 0
    }

    /// `audio_output_open` body: validate, build, start the thread. On
    /// failure returns the C error code and — like the C — leaves nothing
    /// behind.
    ///
    /// # Safety
    /// `info.name` must point to a valid NUL-terminated string that stays
    /// valid for the output's whole life — the shallow `info` copy keeps
    /// the pointer and the worker passes it to `%s` formatting. An input
    /// callback/context given in `info` must stay callable until close.
    ///
    /// An unchecked safe call does not compile:
    /// ```compile_fail
    /// use obs_media_io::audio_io::AudioOutput;
    /// use obs_media_io::ffi::audio_io::audio_output_info;
    /// let info = audio_output_info {
    ///     name: c"x".as_ptr(), samples_per_sec: 48000, format: 4,
    ///     speakers: 2, input_callback: None,
    ///     input_param: core::ptr::null_mut(),
    /// };
    /// let _ = AudioOutput::open(&info);
    /// ```
    pub unsafe fn open(info: &audio_output_info) -> Result<Box<AudioOutput>, c_int> {
        let mut out = Self::build(info)?;

        let ptr = SendShared(&*out.shared as *const Shared);
        let handle = std::thread::Builder::new().spawn(move || audio_thread(ptr.into_inner()));
        match handle {
            Ok(h) => out.thread = Some(h),
            Err(_) => return Err(AUDIO_OUTPUT_FAIL),
        }

        out.initialized = true;
        Ok(out)
    }

    /// Test-only fixture: the same object with no audio thread. A driver
    /// owns every tick via `tick_for_test`, so an explicit tick can never
    /// race the worker's buffer writes or lock acquisitions.
    ///
    /// # Safety
    /// Same `info.name`/input-callback validity contract as `open`; every
    /// `tick_for_test` must additionally come from one driver thread.
    #[doc(hidden)]
    pub unsafe fn open_no_worker(info: &audio_output_info) -> Result<Box<AudioOutput>, c_int> {
        Self::build(info)
    }

    fn build(info: &audio_output_info) -> Result<Box<AudioOutput>, c_int> {
        if !Self::valid_audio_params(info) {
            return Err(AUDIO_OUTPUT_INVALIDPARAM);
        }

        let planar = is_audio_planar(info.format);
        let channels = get_audio_channels(info.speakers) as usize;
        let planes = if planar { channels } else { 1 };
        let block_size =
            (if planar { 1 } else { channels }) * get_audio_bytes_per_channel(info.format);

        let mut shared = Box::new(Shared {
            info: *info,
            channels,
            planes,
            block_size,
            input_cb: info.input_callback,
            input_param: info.input_param,
            stop: AtomicBool::new(false),
            input_mutex: InputMutex::uninit(),
            mixes: std::array::from_fn(|_| AudioMix {
                inputs: UnsafeCell::new(Vec::new()),
                buffer: UnsafeCell::new([0.0; MAX_AUDIO_CHANNELS * AUDIO_OUTPUT_FRAMES]),
                buffer_unclamped: UnsafeCell::new([0.0; MAX_AUDIO_CHANNELS * AUDIO_OUTPUT_FRAMES]),
            }),
        });
        // SAFETY: first and only init, at the mutex's final (boxed) address.
        if unsafe { shared.input_mutex.init() }.is_err() {
            return Err(AUDIO_OUTPUT_FAIL);
        }

        let out = Box::new(AudioOutput {
            shared,
            thread: None,
            initialized: false,
        });
        Ok(out)
    }

    /// `audio_output_connect`. `&self`: the input list lives behind the
    /// shared recursive lock, so callers (including output callbacks on the
    /// audio thread) share one object — never an exclusive `&mut`.
    ///
    /// `callback == NULL` is accepted like the C's connect, but the dead
    /// call is skipped at dispatch instead of faulting (documented
    /// deviation); the input stays active.
    ///
    /// # Safety
    /// `param` must stay valid until a matching `disconnect` or the
    /// output's close — and must outlive any in-flight callback return.
    /// `callback` runs on the audio worker thread (or the single driver
    /// thread in a no-worker fixture) with the recursive input lock held;
    /// it may re-enter `connect`/`disconnect`/`active` on the same thread
    /// only.
    ///
    /// An unchecked safe call does not compile:
    /// ```compile_fail
    /// use obs_media_io::audio_io::AudioOutput;
    /// use obs_media_io::ffi::audio_io::audio_output_info;
    /// let info = audio_output_info {
    ///     name: c"x".as_ptr(), samples_per_sec: 48000, format: 4,
    ///     speakers: 2, input_callback: None,
    ///     input_param: core::ptr::null_mut(),
    /// };
    /// let out = unsafe { AudioOutput::open_no_worker(&info) }.unwrap();
    /// let _ = out.connect(0, None, None, core::ptr::null_mut());
    /// ```
    pub unsafe fn connect(
        &self,
        mi: usize,
        conversion: Option<&crate::ffi::audio_io::audio_convert_info>,
        callback: audio_output_callback_t,
        param: *mut c_void,
    ) -> bool {
        if mi >= MAX_AUDIO_MIXES {
            return false;
        }

        let shared = &*self.shared;
        shared.input_mutex.lock();

        let mut success = false;
        {
            // SAFETY: the lock is held; this borrow ends before unlock.
            let inputs = unsafe { &mut *shared.mixes[mi].inputs.get() };
            if input_index(inputs, callback, param).is_none() {
                let mut input = AudioInput {
                    conversion: crate::ffi::audio_io::audio_convert_info {
                        samples_per_sec: 0,
                        format: 0,
                        speakers: 0,
                        allow_clipping: false,
                    },
                    resampler: ptr::null_mut(),
                    callback,
                    param,
                };

                match conversion {
                    None => {
                        input.conversion.format = shared.info.format;
                        input.conversion.speakers = shared.info.speakers;
                        input.conversion.samples_per_sec = shared.info.samples_per_sec;
                    }
                    Some(c) => input.conversion = *c,
                }

                if input.conversion.format == 0 {
                    input.conversion.format = shared.info.format;
                }
                if input.conversion.speakers == 0 {
                    input.conversion.speakers = shared.info.speakers;
                }
                if input.conversion.samples_per_sec == 0 {
                    input.conversion.samples_per_sec = shared.info.samples_per_sec;
                }

                success = audio_input_init(&mut input, &shared.info);
                if success {
                    inputs.push(input);
                }
            }
        }

        shared.input_mutex.unlock();
        success
    }

    /// `audio_output_disconnect`.
    pub fn disconnect(
        &self,
        mix_idx: usize,
        callback: audio_output_callback_t,
        param: *mut c_void,
    ) {
        if mix_idx >= MAX_AUDIO_MIXES {
            return;
        }

        let shared = &*self.shared;
        shared.input_mutex.lock();

        {
            // SAFETY: the lock is held; this borrow ends before unlock.
            let inputs = unsafe { &mut *shared.mixes[mix_idx].inputs.get() };
            if let Some(idx) = input_index(inputs, callback, param) {
                let mut input = inputs.remove(idx);
                audio_input_free(&mut input);
            }
        }

        shared.input_mutex.unlock();
    }

    /// `audio_output_active`.
    pub fn active(&self) -> bool {
        let shared = &*self.shared;
        shared.input_mutex.lock();
        // SAFETY: the lock is held; borrow ends before unlock.
        let active = unsafe { shared.mixes.iter().any(|m| !(*m.inputs.get()).is_empty()) };
        shared.input_mutex.unlock();
        active
    }

    /// Test hooks for the Tier-3 parity test: the same tick and mix
    /// introspection the C oracle exports as `oracle_audio_*`.
    ///
    /// # Safety
    /// Only on objects from `open_no_worker` — a live worker ticks
    /// concurrently outside this call — and only from a single driver:
    /// `AudioOutput` is `Sync`, so simultaneous `tick_for_test` calls on one
    /// object would overlap buffer writes outside `input_mutex`.
    #[doc(hidden)]
    pub unsafe fn tick_for_test(&self, audio_time: u64, prev_time: u64) {
        self.shared.input_and_output(audio_time, prev_time);
    }

    #[doc(hidden)]
    pub fn mix_input_count_for_test(&self, mix_idx: usize) -> usize {
        let shared = &*self.shared;
        shared.input_mutex.lock();
        // SAFETY: the lock is held; borrow ends before unlock.
        let n = unsafe { (*shared.mixes[mix_idx].inputs.get()).len() };
        shared.input_mutex.unlock();
        n
    }

    #[doc(hidden)]
    pub fn mix_input_resampled_for_test(&self, mix_idx: usize, input_idx: usize) -> bool {
        let shared = &*self.shared;
        shared.input_mutex.lock();
        // SAFETY: the lock is held; borrow ends before unlock.
        let r = unsafe {
            !(&(*shared.mixes[mix_idx].inputs.get()))[input_idx]
                .resampler
                .is_null()
        };
        shared.input_mutex.unlock();
        r
    }

    #[doc(hidden)]
    pub fn mix_input_conversion_for_test(
        &self,
        mix_idx: usize,
        input_idx: usize,
    ) -> crate::ffi::audio_io::audio_convert_info {
        let shared = &*self.shared;
        shared.input_mutex.lock();
        // SAFETY: the lock is held; the struct is copied out before unlock.
        let conv = unsafe { (&(*shared.mixes[mix_idx].inputs.get()))[input_idx].conversion };
        shared.input_mutex.unlock();
        conv
    }
}

impl Drop for AudioOutput {
    /// `audio_output_close`: stop the thread, join it, then free every mix's
    /// resamplers (in mix order, like the C). Stop/join completes before the
    /// shared box is destroyed and never holds the input lock.
    fn drop(&mut self) {
        if self.initialized {
            self.shared.stop.store(true, Ordering::SeqCst);
            if let Some(h) = self.thread.take() {
                let _ = h.join();
            }
        }
        // Worker is dead; exclusive access again.
        for mix in self.shared.mixes.iter() {
            // SAFETY: nothing shares the object once drop reaches here.
            for input in unsafe { &mut *mix.inputs.get() } {
                audio_input_free(input);
            }
        }
    }
}

/// The audio thread: `&Shared` for its whole life — never `&mut` and never
/// the owner object.
fn audio_thread(shared: *const Shared) {
    // SAFETY: the Shared box outlives the thread — AudioOutput::drop joins
    // before tearing it down.
    let s = unsafe { &*shared };
    let rate = s.info.samples_per_sec as usize;
    let mut samples: u64 = 0;
    let start_time = unsafe { os_gettime_ns() };
    let mut prev_time = start_time;

    unsafe { os_set_thread_name(c"audio-io: audio thread".as_ptr()) };

    let audio_thread_name = unsafe {
        profile_store_name(
            obs_get_profiler_name_store(),
            c"audio_thread(%s)".as_ptr(),
            s.info.name,
        )
    };

    while !s.stop.load(Ordering::SeqCst) {
        samples += AUDIO_OUTPUT_FRAMES as u64;
        let audio_time =
            start_time.wrapping_add(mul_div64(samples, 1_000_000_000, rate as u64));

        unsafe { os_sleepto_ns_fast(audio_time) };

        unsafe { profile_start(audio_thread_name) };

        s.input_and_output(audio_time, prev_time);
        prev_time = audio_time;

        unsafe { profile_end(audio_thread_name) };

        unsafe { profile_reenable_thread() };
    }
}
}

#[cfg(unix)]
pub use port::*;
