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

use core::ffi::{c_char, c_int, c_void};
use core::ptr;
use std::cell::UnsafeCell;
use std::sync::atomic::{AtomicBool, Ordering};
use std::thread::JoinHandle;

use crate::ffi::audio_io::{
    AUDIO_OUTPUT_FAIL, AUDIO_OUTPUT_INVALIDPARAM, audio_data, audio_input_callback_t,
    audio_output_callback_t, audio_output_data, audio_output_info, resample_info,
};
use crate::video_io::MAX_AV_PLANES;

pub const MAX_AUDIO_MIXES: usize = 6;
pub const MAX_AUDIO_CHANNELS: usize = 8;
pub const AUDIO_OUTPUT_FRAMES: usize = 1024;

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

/// `util_mul_div64` (static inline in `util/util_uint64.h`): the portable
/// form both non-MSVC builds use.
fn util_mul_div64(num: u64, mul: u64, div: u64) -> u64 {
    let rem = num % div;
    (num / div) * mul + (rem * mul) / div
}

/// `get_audio_channels` (static inline in `media-io/audio-io.h`).
fn get_audio_channels(speakers: c_int) -> usize {
    match speakers {
        1 => 1,
        2 => 2,
        3 => 3,
        4 => 4,
        5 => 5,
        6 => 6,
        8 => 8,
        _ => 0,
    }
}

/// `get_audio_bytes_per_channel` (static inline in `media-io/audio-io.h`).
fn get_audio_bytes_per_channel(format: c_int) -> usize {
    match format {
        1 | 5 => 1,
        2 | 6 => 2,
        3 | 4 | 7 | 8 => 4,
        _ => 0,
    }
}

/// `is_audio_planar` (static inline in `media-io/audio-io.h`).
fn is_audio_planar(format: c_int) -> bool {
    matches!(format, 5..=8)
}

/// A recursive mutex: `audio_output_connect`/`disconnect` lock `input_mutex`
/// and the C comments in `do_audio_output` let an output callback disconnect
/// or connect again from inside the audio thread, which only works because
/// `pthread_mutex_init_recursive` allows same-thread re-lock.
struct RecursiveMutex(UnsafeCell<libc::pthread_mutex_t>);

unsafe impl Send for RecursiveMutex {}
unsafe impl Sync for RecursiveMutex {}

impl RecursiveMutex {
    fn new() -> Result<Self, c_int> {
        unsafe {
            let mut mutex: libc::pthread_mutex_t = std::mem::zeroed();
            let mut attr: libc::pthread_mutexattr_t = std::mem::zeroed();
            if libc::pthread_mutexattr_init(&mut attr) != 0 {
                return Err(-1);
            }
            libc::pthread_mutexattr_settype(&mut attr, libc::PTHREAD_MUTEX_RECURSIVE);
            let rc = libc::pthread_mutex_init(&mut mutex, &attr);
            libc::pthread_mutexattr_destroy(&mut attr);
            if rc != 0 {
                return Err(-1);
            }
            Ok(Self(UnsafeCell::new(mutex)))
        }
    }

    #[inline]
    fn lock(&self) {
        unsafe { libc::pthread_mutex_lock(self.0.get()) };
    }

    #[inline]
    fn unlock(&self) {
        unsafe { libc::pthread_mutex_unlock(self.0.get()) };
    }
}

impl Drop for RecursiveMutex {
    fn drop(&mut self) {
        unsafe { libc::pthread_mutex_destroy(self.0.get()) };
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
/// buffers stored flat like the C's contiguous rows.
struct AudioMix {
    inputs: Vec<AudioInput>,
    buffer: [f32; MAX_AUDIO_CHANNELS * AUDIO_OUTPUT_FRAMES],
    buffer_unclamped: [f32; MAX_AUDIO_CHANNELS * AUDIO_OUTPUT_FRAMES],
}

/// `struct audio_output`. `info` keeps the caller's `struct audio_output_info`
/// verbatim so `audio_output_get_info` echoes it — including `name`'s pointer,
/// like the C's `memcpy`.
pub struct AudioOutput {
    pub(crate) info: audio_output_info,
    pub(crate) block_size: usize,
    pub(crate) channels: usize,
    pub(crate) planes: usize,

    stop: AtomicBool,
    thread: Option<JoinHandle<()>>,
    initialized: bool,

    input_cb: audio_input_callback_t,
    input_param: *mut c_void,
    input_mutex: RecursiveMutex,
    mixes: [AudioMix; MAX_AUDIO_MIXES],
}

// Raw C pointers are passed across the API (param, callback, resampler); the
// audio thread and callers share the struct exactly like the C does.
unsafe impl Send for AudioOutput {}

struct SendOutput(*mut AudioOutput);
unsafe impl Send for SendOutput {}

impl SendOutput {
    fn into_inner(self) -> *mut AudioOutput {
        self.0
    }
}

fn resample_audio_output(input: &mut AudioInput, data: &mut audio_data) -> bool {
    let mut success = true;

    if !input.resampler.is_null() {
        let mut output: [*mut u8; MAX_AV_PLANES] = [ptr::null_mut(); MAX_AV_PLANES];
        let mut frames: u32 = 0;
        let mut offset: u64 = 0;

        success = unsafe {
            audio_resampler_resample(
                input.resampler,
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
    }

    success
}

/// `audio_input_free`.
fn audio_input_free(input: &mut AudioInput) {
    unsafe { audio_resampler_destroy(input.resampler) };
}

impl AudioOutput {
    fn do_audio_output(&mut self, mix_idx: usize, timestamp: u64, frames: u32) {
        let planes = self.planes;
        let mix = &mut self.mixes[mix_idx];

        self.input_mutex.lock();

        let mut data = audio_data {
            data: [ptr::null_mut(); MAX_AV_PLANES],
            frames: 0,
            timestamp: 0,
        };

        for input in mix.inputs.iter_mut().rev() {
            let buf: &mut [f32] = if input.conversion.allow_clipping {
                &mut mix.buffer_unclamped
            } else {
                &mut mix.buffer
            };
            let buf_ptr = buf.as_mut_ptr();
            for (i, slot) in data.data.iter_mut().enumerate().take(planes) {
                *slot = unsafe { buf_ptr.add(i * AUDIO_OUTPUT_FRAMES) } as *mut u8;
            }

            data.frames = frames;
            data.timestamp = timestamp;

            if resample_audio_output(input, &mut data)
                && let Some(callback) = input.callback
            {
                unsafe { callback(input.param, mix_idx, &mut data) };
            }
        }

        self.input_mutex.unlock();
    }

    fn clamp_audio_output(&mut self, bytes: usize) {
        let float_size = bytes / core::mem::size_of::<f32>();

        for mix in &mut self.mixes {
            if mix.inputs.is_empty() {
                continue;
            }

            for plane in 0..self.planes {
                let start = plane * AUDIO_OUTPUT_FRAMES;
                let end = start + float_size;
                mix.buffer_unclamped[start..end].copy_from_slice(&mix.buffer[start..end]);
                for val in &mut mix.buffer[start..end] {
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
    fn input_and_output(&mut self, audio_time: u64, prev_time: u64) {
        let bytes = AUDIO_OUTPUT_FRAMES * self.block_size;
        let mut data = [audio_output_data {
            data: [ptr::null_mut(); MAX_AUDIO_CHANNELS],
        }; MAX_AUDIO_MIXES];
        let mut active_mixes: u32 = 0;
        let mut new_ts: u64 = 0;

        self.input_mutex.lock();
        for (i, mix) in self.mixes.iter().enumerate() {
            if !mix.inputs.is_empty() {
                active_mixes |= 1 << i;
            }
        }
        self.input_mutex.unlock();

        for (mix_idx, mix) in self.mixes.iter_mut().enumerate() {
            mix.buffer.fill(0.0);
            for (i, slot) in data[mix_idx].data.iter_mut().enumerate().take(self.planes) {
                *slot = unsafe { mix.buffer.as_mut_ptr().add(i * AUDIO_OUTPUT_FRAMES) };
            }
        }

        let success = unsafe {
            (self.input_cb.unwrap())(
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

        self.clamp_audio_output(bytes);

        for i in 0..MAX_AUDIO_MIXES {
            self.do_audio_output(i, new_ts, AUDIO_OUTPUT_FRAMES as u32);
        }
    }

    /// `audio_get_input_idx`: position of (callback, param) in a mix, or None
    /// like the C's `DARRAY_INVALID`.
    fn get_input_idx(
        &self,
        mix_idx: usize,
        callback: audio_output_callback_t,
        param: *mut c_void,
    ) -> Option<usize> {
        self.mixes[mix_idx].inputs.iter().position(|i| {
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
    fn audio_input_init(input: &mut AudioInput, audio: &AudioOutput) -> bool {
        if input.conversion.format != audio.info.format
            || input.conversion.samples_per_sec != audio.info.samples_per_sec
            || input.conversion.speakers != audio.info.speakers
        {
            let from = resample_info {
                format: audio.info.format,
                samples_per_sec: audio.info.samples_per_sec,
                speakers: audio.info.speakers,
            };
            let to = resample_info {
                format: input.conversion.format,
                samples_per_sec: input.conversion.samples_per_sec,
                speakers: input.conversion.speakers,
            };

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

    /// `valid_audio_params`.
    fn valid_audio_params(info: &audio_output_info) -> bool {
        info.format != 0 && !info.name.is_null() && info.samples_per_sec > 0 && info.speakers > 0
    }

    /// `audio_output_open` body: validate, build, start the thread. On failure
    /// returns the C error code and — like the C — leaves `*audio` untouched.
    /// # Safety
    /// `info` must point to a valid `struct audio_output_info`.
    pub unsafe fn open(info: *mut audio_output_info) -> Result<Box<AudioOutput>, c_int> {
        let i = unsafe { &*info };
        if !Self::valid_audio_params(i) {
            return Err(AUDIO_OUTPUT_INVALIDPARAM);
        }

        let planar = is_audio_planar(i.format);
        let channels = get_audio_channels(i.speakers);
        let planes = if planar { channels } else { 1 };
        let block_size =
            (if planar { 1 } else { channels }) * get_audio_bytes_per_channel(i.format);

        let input_mutex = match RecursiveMutex::new() {
            Ok(m) => m,
            Err(_) => return Err(AUDIO_OUTPUT_FAIL),
        };

        let mut out = Box::new(AudioOutput {
            info: *i,
            channels,
            planes,
            block_size,
            stop: AtomicBool::new(false),
            thread: None,
            initialized: false,
            input_cb: i.input_callback,
            input_param: i.input_param,
            input_mutex,
            mixes: std::array::from_fn(|_| AudioMix {
                inputs: Vec::new(),
                buffer: [0.0; MAX_AUDIO_CHANNELS * AUDIO_OUTPUT_FRAMES],
                buffer_unclamped: [0.0; MAX_AUDIO_CHANNELS * AUDIO_OUTPUT_FRAMES],
            }),
        });

        let ptr = SendOutput(&mut *out as *mut AudioOutput);
        let handle =
            std::thread::Builder::new().spawn(move || unsafe { audio_thread(ptr.into_inner()) });
        match handle {
            Ok(h) => out.thread = Some(h),
            Err(_) => return Err(AUDIO_OUTPUT_FAIL),
        }

        out.initialized = true;
        Ok(out)
    }

    /// `audio_output_connect`.
    ///
    /// # Safety
    /// `conversion`, when non-NULL, must point to a valid `struct audio_convert_info`.
    pub unsafe fn connect(
        &mut self,
        mi: usize,
        conversion: *const crate::ffi::audio_io::audio_convert_info,
        callback: audio_output_callback_t,
        param: *mut c_void,
    ) -> bool {
        if mi >= MAX_AUDIO_MIXES {
            return false;
        }

        self.input_mutex.lock();

        let mut success = false;
        if self.get_input_idx(mi, callback, param).is_none() {
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

            if conversion.is_null() {
                input.conversion.format = self.info.format;
                input.conversion.speakers = self.info.speakers;
                input.conversion.samples_per_sec = self.info.samples_per_sec;
            } else {
                input.conversion = unsafe { *conversion };
            }

            if input.conversion.format == 0 {
                input.conversion.format = self.info.format;
            }
            if input.conversion.speakers == 0 {
                input.conversion.speakers = self.info.speakers;
            }
            if input.conversion.samples_per_sec == 0 {
                input.conversion.samples_per_sec = self.info.samples_per_sec;
            }

            success = Self::audio_input_init(&mut input, self);
            if success {
                self.mixes[mi].inputs.push(input);
            }
        }

        self.input_mutex.unlock();
        success
    }

    /// `audio_output_disconnect`.
    pub fn disconnect(
        &mut self,
        mix_idx: usize,
        callback: audio_output_callback_t,
        param: *mut c_void,
    ) {
        if mix_idx >= MAX_AUDIO_MIXES {
            return;
        }

        self.input_mutex.lock();

        if let Some(idx) = self.get_input_idx(mix_idx, callback, param) {
            let mut input = self.mixes[mix_idx].inputs.remove(idx);
            audio_input_free(&mut input);
        }

        self.input_mutex.unlock();
    }

    /// `audio_output_active`.
    pub fn active(&self) -> bool {
        self.mixes.iter().any(|m| !m.inputs.is_empty())
    }

    /// Test hooks for the Tier-3 parity test: the same tick and mix
    /// introspection the C oracle exports as `oracle_audio_*`.
    #[doc(hidden)]
    pub fn tick_for_test(&mut self, audio_time: u64, prev_time: u64) {
        self.input_and_output(audio_time, prev_time);
    }

    #[doc(hidden)]
    pub fn mix_input_count_for_test(&self, mix_idx: usize) -> usize {
        self.mixes[mix_idx].inputs.len()
    }

    #[doc(hidden)]
    pub fn mix_input_resampled_for_test(&self, mix_idx: usize, input_idx: usize) -> bool {
        !self.mixes[mix_idx].inputs[input_idx].resampler.is_null()
    }

    #[doc(hidden)]
    pub fn mix_input_conversion_for_test(
        &self,
        mix_idx: usize,
        input_idx: usize,
    ) -> &crate::ffi::audio_io::audio_convert_info {
        &self.mixes[mix_idx].inputs[input_idx].conversion
    }
}

impl Drop for AudioOutput {
    /// `audio_output_close`: stop the thread, join it, then free every mix's
    /// resamplers (in mix order, like the C).
    fn drop(&mut self) {
        if self.initialized {
            self.stop.store(true, Ordering::SeqCst);
            if let Some(h) = self.thread.take() {
                let _ = h.join();
            }
        }
        for mix in &mut self.mixes {
            for input in &mut mix.inputs {
                audio_input_free(input);
            }
        }
    }
}

unsafe fn audio_thread(audio: *mut AudioOutput) {
    let a = unsafe { &mut *audio };
    let rate = a.info.samples_per_sec as usize;
    let mut samples: u64 = 0;
    let start_time = unsafe { os_gettime_ns() };
    let mut prev_time = start_time;

    unsafe { os_set_thread_name(c"audio-io: audio thread".as_ptr()) };

    let audio_thread_name = unsafe {
        profile_store_name(
            obs_get_profiler_name_store(),
            c"audio_thread(%s)".as_ptr(),
            a.info.name,
        )
    };

    while !a.stop.load(Ordering::SeqCst) {
        samples += AUDIO_OUTPUT_FRAMES as u64;
        let audio_time =
            start_time.wrapping_add(util_mul_div64(samples, 1_000_000_000, rate as u64));

        unsafe { os_sleepto_ns_fast(audio_time) };

        unsafe { profile_start(audio_thread_name) };

        a.input_and_output(audio_time, prev_time);
        prev_time = audio_time;

        unsafe { profile_end(audio_thread_name) };

        unsafe { profile_reenable_thread() };
    }
}
