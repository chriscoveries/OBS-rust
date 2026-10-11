//! Tier 1 on the safe core: every call below goes through [`AudioOutput`]'s
//! safe methods — no `unsafe` at the call sites (the extern "C" callbacks
//! themselves keep the `unsafe` marker their ABI requires).
//!
//! The audio thread and the callbacks run for real here: `os_*` timing and
//! the profiler/resampler symbols resolve into the oracle staticlib's POSIX
//! threading build plus its stubs, like the FFI-level Tier 1 file.
//!
//! Unix-first: the port is `#[cfg(unix)]`; Windows keeps media-io/audio-io.c.

#![cfg(unix)]

use std::ptr;
use std::sync::Mutex;
use std::sync::atomic::{AtomicUsize, Ordering};

/// Serializes the explicit-tick tests: the real audio worker never produces a
/// MAGIC start_ts, but the shared counters can still race across parallel
/// test bodies (several `tick_for_test(MAGIC)` calls at once).
static DRIVER: Mutex<()> = Mutex::new(());

use obs_c_oracle as _;
use obs_media_io::audio_io::AudioOutput;
use obs_media_io::ffi::audio_io::{
    audio_convert_info, audio_data, audio_output_data, audio_output_info,
};
use obs_util as _;

const FORMAT_FLOAT: i32 = 4;
const SPEAKERS_STEREO: i32 = 2;
/// Explicit-tick timestamps the counting callbacks accept; the real audio
/// thread's own ticks are ignored by checking `start_ts` against this.
const MAGIC: u64 = 0x1eaf_dead_beef;

static INPUT_CALLS: AtomicUsize = AtomicUsize::new(0);
static OUTPUT_CALLS: AtomicUsize = AtomicUsize::new(0);

fn lock_driver() -> std::sync::MutexGuard<'static, ()> {
    DRIVER.lock().unwrap_or_else(|e| e.into_inner())
}

fn reset_counts() {
    INPUT_CALLS.store(0, Ordering::SeqCst);
    OUTPUT_CALLS.store(0, Ordering::SeqCst);
}

unsafe extern "C" fn counting_input(
    _param: *mut core::ffi::c_void,
    start_ts: u64,
    end_ts: u64,
    new_ts: *mut u64,
    _active: u32,
    mixes: *mut audio_output_data,
) -> bool {
    if start_ts == MAGIC {
        INPUT_CALLS.fetch_add(1, Ordering::SeqCst);
        unsafe {
            *new_ts = end_ts;
            *(*mixes).data[0] = 0.5;
        }
    }
    true
}

unsafe extern "C" fn counting_output(
    _param: *mut core::ffi::c_void,
    _mix_idx: usize,
    _data: *mut audio_data,
) {
    OUTPUT_CALLS.fetch_add(1, Ordering::SeqCst);
}

fn stereo_info() -> audio_output_info {
    audio_output_info {
        name: c"core".as_ptr(),
        samples_per_sec: 48000,
        format: FORMAT_FLOAT,
        speakers: SPEAKERS_STEREO,
        input_callback: Some(counting_input),
        input_param: ptr::null_mut(),
    }
}

/// Mutation guard: a tick must actually dispatch the output callback —
/// deleting the call site in `do_audio_output` fails this test.
#[test]
fn output_callback_runs_each_tick() {
    let _drive = lock_driver();
    reset_counts();
    let out = AudioOutput::open(&stereo_info()).expect("open");
    assert!(out.connect(0, None, Some(counting_output), ptr::null_mut()));

    out.tick_for_test(MAGIC + 21_000_000, MAGIC);

    assert_eq!(INPUT_CALLS.load(Ordering::SeqCst), 1);
    assert_eq!(OUTPUT_CALLS.load(Ordering::SeqCst), 1);
    assert!(out.active());
}

/// `input_callback == NULL`: the C calls the pointer unconditionally and
/// faults inside `input_and_output`. The port documents the deviation as a
/// no-op tick — it must never panic.
#[test]
fn tick_without_input_callback_is_noop() {
    let _drive = lock_driver();
    let mut info = stereo_info();
    info.input_callback = None;
    let out = AudioOutput::open(&info).expect("open");
    out.tick_for_test(MAGIC + 21_000_000, MAGIC);
}

/// `callback == NULL` output input: the C accepts the connect and then
/// dereferences NULL on the next tick. The port accepts the connect (same
/// return value) but skips the dead call and keeps the input active —
/// documented deviation.
#[test]
fn null_output_callback_connects_and_stays_silent() {
    let _drive = lock_driver();
    reset_counts();
    let out = AudioOutput::open(&stereo_info()).expect("open");
    assert!(out.connect(0, None, None, ptr::null_mut()));
    assert!(out.active());

    out.tick_for_test(MAGIC + 21_000_000, MAGIC);
    assert_eq!(OUTPUT_CALLS.load(Ordering::SeqCst), 0);
}

/// An output callback may disconnect its own input mid-dispatch (the C's
/// recursive input_mutex exists exactly for this). The live-list walk must
/// not corrupt or double-run a remaining input.
#[test]
fn output_callback_may_disconnect_itself() {
    let _drive = lock_driver();
    struct Ctx {
        audio: *const AudioOutput,
        hits: AtomicUsize,
    }
    unsafe impl Sync for Ctx {}

    unsafe extern "C" fn self_disconnecting(
        param: *mut core::ffi::c_void,
        _mix_idx: usize,
        _data: *mut audio_data,
    ) {
        let ctx = unsafe { &*(param as *const Ctx) };
        ctx.hits.fetch_add(1, Ordering::SeqCst);
        unsafe { &*ctx.audio }.disconnect(0, Some(self_disconnecting), param);
    }

    let out = Box::new(AudioOutput::open(&stereo_info()).expect("open"));
    let ctx = Ctx {
        audio: &**out,
        hits: AtomicUsize::new(0),
    };
    assert!(out.connect(
        0,
        None,
        Some(self_disconnecting),
        &ctx as *const Ctx as *mut core::ffi::c_void
    ));

    out.tick_for_test(MAGIC + 21_000_000, MAGIC);

    assert_eq!(ctx.hits.load(Ordering::SeqCst), 1);
    assert!(!out.active());
    // `ctx` stays live in this frame until here: the callback param points at it.
    let _ = &ctx;
}

/// An output callback may connect a new input mid-dispatch (recursive lock
/// again); the new input is appended and gets its first callback on the
/// NEXT tick, like the C's fixed loop bound.
#[test]
fn output_callback_may_connect_a_new_input() {
    let _drive = lock_driver();
    struct Ctx {
        audio: *const AudioOutput,
        second: AtomicUsize,
    }
    unsafe impl Sync for Ctx {}

    unsafe extern "C" fn second_cb(
        param: *mut core::ffi::c_void,
        _mix_idx: usize,
        _data: *mut audio_data,
    ) {
        let ctx = unsafe { &*(param as *const Ctx) };
        ctx.second.fetch_add(1, Ordering::SeqCst);
    }

    unsafe extern "C" fn connector(
        param: *mut core::ffi::c_void,
        _mix_idx: usize,
        _data: *mut audio_data,
    ) {
        let ctx = unsafe { &*(param as *const Ctx) };
        unsafe { &*ctx.audio }.connect(0, None, Some(second_cb), param);
    }

    let out = Box::new(AudioOutput::open(&stereo_info()).expect("open"));
    let ctx = Box::new(Ctx {
        audio: &**out,
        second: AtomicUsize::new(0),
    });
    let param = &*ctx as *const Ctx as *mut core::ffi::c_void;
    assert!(out.connect(0, None, Some(connector), param));

    out.tick_for_test(MAGIC + 21_000_000, MAGIC);
    // C's loop bound is fixed at dispatch start: the appended input is not
    // invoked this tick.
    assert_eq!(ctx.second.load(Ordering::SeqCst), 0);

    out.tick_for_test(MAGIC + 42_000_000, MAGIC);
    assert!(ctx.second.load(Ordering::SeqCst) >= 1);
    // `ctx` must outlive the last tick: the callback param points at it.
    let _ = &ctx;
}

/// The clamp pass: an unclamped input (allow_clipping=false, the default)
/// sees the [-1,1] clamped buffer with NaN folded to 0; an allow_clipping
/// input sees the raw unclamped copy. Deleting the clamp fails this test.
#[test]
fn clamping_respects_allow_clipping() {
    let _drive = lock_driver();
    static SEEN: [AtomicUsize; 2] = [AtomicUsize::new(0), AtomicUsize::new(0)];

    unsafe extern "C" fn writing_input(
        _param: *mut core::ffi::c_void,
        start_ts: u64,
        end_ts: u64,
        new_ts: *mut u64,
        _active: u32,
        mixes: *mut audio_output_data,
    ) -> bool {
        if start_ts == MAGIC {
            unsafe {
                *new_ts = end_ts;
                (*mixes).data[0].add(0).write(2.5);
                (*mixes).data[0].add(1).write(f32::NAN);
            }
        }
        true
    }

    fn record(slot: usize, _mix_idx: usize, data: *mut audio_data, dst: &AtomicUsize) {
        let bits = unsafe { *(*data).data[0].cast::<f32>() }.to_bits() as usize;
        dst.store(bits, Ordering::SeqCst);
        let _ = slot;
    }

    unsafe extern "C" fn clipped_cb(
        _param: *mut core::ffi::c_void,
        mix_idx: usize,
        data: *mut audio_data,
    ) {
        record(0, mix_idx, data, &SEEN[0]);
    }

    unsafe extern "C" fn raw_cb(
        _param: *mut core::ffi::c_void,
        mix_idx: usize,
        data: *mut audio_data,
    ) {
        record(1, mix_idx, data, &SEEN[1]);
    }

    let mut info = stereo_info();
    info.input_callback = Some(writing_input);
    let out = AudioOutput::open(&info).expect("open");
    assert!(out.connect(0, None, Some(clipped_cb), ptr::null_mut()));
    let conv = audio_convert_info {
        samples_per_sec: 0,
        format: 0,
        speakers: 0,
        allow_clipping: true,
    };
    assert!(out.connect(0, Some(&conv), Some(raw_cb), ptr::null_mut()));

    out.tick_for_test(MAGIC + 21_000_000, MAGIC);

    assert_eq!(SEEN[0].load(Ordering::SeqCst), 1.0f32.to_bits() as usize);
    assert_eq!(SEEN[1].load(Ordering::SeqCst), 2.5f32.to_bits() as usize);
}
