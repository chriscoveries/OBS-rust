//! Tier 3: the Rust audio-io C ABI behaves exactly like the original C,
//! compiled as an oracle. State is compared through identical call
//! sequences; ticks are driven explicitly on NO-WORKER fixtures
//! (`oracle_audio_output_open_no_thread` / `AudioOutput::open_no_worker`),
//! so the driver owns the tick path exclusively — no audio thread races
//! buffer fills, the input lock, or the recorded state. `is_magic_ts`
//! still gates every record so no stray timestamp can pollute it.
//!
//! The oracle supplies a deterministic resampler (nearest-sample by the
//! src:dst rate ratio), so a rate-differing conversion exercises the real
//! resample path on both sides.
//!
use core::ffi::{c_char, c_int, c_void};
use core::ptr;
use std::sync::Mutex;
use std::sync::atomic::{AtomicPtr, AtomicUsize, Ordering};

use obs_c_oracle as _;
use obs_c_oracle::audio_io as c;
use obs_media_io::audio_io::AudioOutput;
use obs_media_io::ffi::audio_io as rs;
use obs_util as _;
use proptest::prelude::*;

const FORMAT_FLOAT: c_int = 4;
const SPEAKERS_STEREO: c_int = 2;

/// `audio_time`/`prev_time` pair used only by explicit ticks; callbacks
/// ignore every invocation whose `start_ts` is not this magic value.
const MAGIC: u64 = 0x1eaf_dead_beef;

/// True only for timestamps produced by explicit `*_tick(MAGIC+d)` calls.
/// Worker threads (none in these fixtures) carry `os_gettime_ns` values
/// (~1e18 ns); every recorded action below checks this gate regardless, so
/// a callback invoked anywhere else can never inject a record.
fn is_magic_ts(ts: u64) -> bool {
    (MAGIC..MAGIC.wrapping_add(1_000_000_000)).contains(&ts)
}

/// Serializes the whole test driver: every test holds this for its entire
/// body so parallel `#[test]`s can't interleave into the shared
/// TICK/ORDER state.
static DRIVER: Mutex<()> = Mutex::new(());

#[derive(Debug, PartialEq, Eq)]
struct TickRecord {
    end_ts: u64,
    new_ts: u64,
    active_mixes: u32,
    calls: Vec<(usize, u32, u32)>, // (mix_idx, frames, first sample bits)
}

static TICK: Mutex<Option<TickRecord>> = Mutex::new(None);

fn take_tick() -> Option<TickRecord> {
    TICK.lock().unwrap().take()
}

/// Per-callback event order for the reentrancy regressions — a small tag
/// per invocation, compared verbatim between oracle and port.
static ORDER: Mutex<Vec<u32>> = Mutex::new(Vec::new());

fn push_order(tag: u32) {
    ORDER.lock().unwrap().push(tag);
}

fn take_order() -> Vec<u32> {
    std::mem::take(&mut *ORDER.lock().unwrap())
}

fn record_input(start: u64, end: u64, new_ts: *mut u64, active: u32) {
    if start != MAGIC {
        return;
    }
    // Mirror the C test's `counting_input`: stamp `new_ts` and write one
    // known sample per output mix below (done in the output callback).
    unsafe { *new_ts = end };
    let mut g = TICK.lock().unwrap();
    // Each driven tick starts a fresh record: tests that never `take_tick()`
    // would otherwise leave a stale record behind, silently merging its
    // end_ts/active_mixes/calls into the next test's oracle record.
    *g = Some(TickRecord {
        end_ts: end,
        new_ts: end,
        active_mixes: active,
        calls: Vec::new(),
    });
}

unsafe extern "C" fn input_rs(
    _param: *mut c_void,
    start: u64,
    end: u64,
    new_ts: *mut u64,
    active: u32,
    mixes: *mut rs::audio_output_data,
) -> bool {
    if start == MAGIC {
        record_input(start, end, new_ts, active);
        // Put a marker in mix 0 channel 0 so the output stage has data.
        unsafe { *(*mixes).data[0] = 0.5f32 };
    }
    true
}

unsafe extern "C" fn input_c(
    _param: *mut c_void,
    start: u64,
    end: u64,
    new_ts: *mut u64,
    active: u32,
    mixes: *mut c::OracleAudioOutputData,
) -> bool {
    if start == MAGIC {
        record_input(start, end, new_ts, active);
        unsafe { *(*mixes).data[0] = 0.5f32 };
    }
    true
}

fn record_output(mix_idx: usize, ts: u64, frames: u32, sample: *const f32) {
    if !is_magic_ts(ts) {
        return;
    }
    let mut g = TICK.lock().unwrap();
    if let Some(t) = g.as_mut() {
        t.calls
            .push((mix_idx, frames, unsafe { *sample }.to_bits()));
    }
}

unsafe extern "C" fn output_rs(_p: *mut c_void, mix_idx: usize, data: *mut rs::audio_data) {
    let d = unsafe { &*data };
    record_output(mix_idx, d.timestamp, d.frames, d.data[0] as *const f32);
}

unsafe extern "C" fn output_c(_p: *mut c_void, mix_idx: usize, data: *mut c::OracleAudioData) {
    let d = unsafe { &*data };
    record_output(mix_idx, d.timestamp, d.frames, d.data[0] as *const f32);
}

fn info_rs(cb: rs::audio_input_callback_t) -> rs::audio_output_info {
    rs::audio_output_info {
        name: c"parity".as_ptr(),
        samples_per_sec: 48000,
        format: FORMAT_FLOAT,
        speakers: SPEAKERS_STEREO,
        input_callback: cb,
        input_param: ptr::null_mut(),
    }
}

fn info_c(cb: c::OracleAudioOutputCallback) -> c::OracleAudioOutputInfo {
    // SAFETY: layout-identical signature to input_c; rustfmt-friendly cast.
    let cb = unsafe {
        core::mem::transmute::<
            c::OracleAudioOutputCallback,
            Option<
                unsafe extern "C" fn(
                    *mut c_void,
                    u64,
                    u64,
                    *mut u64,
                    u32,
                    *mut c::OracleAudioOutputData,
                ) -> bool,
            >,
        >(cb)
    };
    c::OracleAudioOutputInfo {
        name: c"parity".as_ptr() as *const c_char,
        samples_per_sec: 48000,
        format: FORMAT_FLOAT,
        speakers: SPEAKERS_STEREO,
        input_callback: cb,
        input_param: ptr::null_mut(),
    }
}

/// Open both sides with NO audio thread: every tick a test sees is one it
/// drove explicitly, so the driver owns the tick path exclusively — no
/// worker races buffer fills, the lock, or the recorded state.
/// Returns the oracle handle and the Rust handle (`*mut c_void` `audio_t`).
unsafe fn open_pair() -> (*mut c::OracleAudioOutput, *mut c_void) {
    let mut co: *mut c::OracleAudioOutput = ptr::null_mut();
    let mut info = info_c(None);
    info.input_callback = Some(input_c);
    assert_eq!(
        unsafe { c::oracle_audio_output_open_no_thread(&mut co, &mut info) },
        0,
        "oracle open"
    );
    let out = unsafe { AudioOutput::open_no_worker(&info_rs(Some(input_rs))) }.expect("rust open");
    let ro = Box::into_raw(out) as *mut c_void;
    (co, ro)
}

/// Close the no-worker pair (counterparts of `open_pair`).
unsafe fn close_pair(co: *mut c::OracleAudioOutput, ro: *mut c_void) {
    unsafe {
        c::oracle_audio_output_close_no_thread(co);
        rs::audio_output_close(ro);
    }
}

/// The C ABI in parameter order on both sides.
#[test]
fn getters_match() {
    let _drive = DRIVER.lock().unwrap_or_else(|e| e.into_inner());
    unsafe {
        let (co, ro) = open_pair();
        assert_eq!(
            c::oracle_audio_output_active(co),
            rs::audio_output_active(ro)
        );
        assert_eq!(
            c::oracle_audio_output_get_block_size(co),
            rs::audio_output_get_block_size(ro)
        );
        assert_eq!(
            c::oracle_audio_output_get_planes(co),
            rs::audio_output_get_planes(ro)
        );
        assert_eq!(
            c::oracle_audio_output_get_channels(co),
            rs::audio_output_get_channels(ro)
        );
        assert_eq!(
            c::oracle_audio_output_get_sample_rate(co),
            rs::audio_output_get_sample_rate(ro)
        );
        let ci = &*c::oracle_audio_output_get_info(co);
        let ri = &*rs::audio_output_get_info(ro);
        assert_eq!(ci.samples_per_sec, ri.samples_per_sec);
        assert_eq!(ci.format, ri.format);
        assert_eq!(ci.speakers, ri.speakers);
        close_pair(co, ro);
    }
}

#[test]
fn connect_disconnect_match() {
    let _drive = DRIVER.lock().unwrap_or_else(|e| e.into_inner());
    unsafe {
        let (co, ro) = open_pair();
        // NULL conversion + NULL param: connects, no resampler.
        assert_eq!(
            c::oracle_audio_output_connect(co, 0, ptr::null(), Some(output_c), ptr::null_mut()),
            rs::audio_output_connect(ro, 0, ptr::null(), Some(output_rs), ptr::null_mut())
        );
        // Duplicate refused on both.
        assert_eq!(
            c::oracle_audio_output_connect(co, 0, ptr::null(), Some(output_c), ptr::null_mut()),
            rs::audio_output_connect(ro, 0, ptr::null(), Some(output_rs), ptr::null_mut())
        );
        assert_eq!(
            c::oracle_audio_output_active(co),
            rs::audio_output_active(ro)
        );
        // Conversion differing from the output format: resampler_create is
        // stubbed NULL, so both refuse and the input is not added.
        let conv = rs::audio_convert_info {
            samples_per_sec: 44100,
            format: FORMAT_FLOAT,
            speakers: SPEAKERS_STEREO,
            allow_clipping: false,
        };
        let cconv = c::OracleAudioConvertInfo {
            samples_per_sec: 44100,
            format: FORMAT_FLOAT,
            speakers: SPEAKERS_STEREO,
            allow_clipping: false,
        };
        assert_eq!(
            c::oracle_audio_output_connect(co, 1, &cconv, Some(output_c), ptr::null_mut()),
            rs::audio_output_connect(ro, 1, &conv, Some(output_rs), ptr::null_mut())
        );
        // Introspection: input counts per mix match.
        let ra = ro as *mut AudioOutput;
        for mix in 0..6 {
            assert_eq!(
                c::oracle_audio_mix_input_count(co, mix),
                (*ra).mix_input_count_for_test(mix),
                "mix {mix}"
            );
        }
        // The surviving input's stored conversion matches C's filled defaults.
        assert_eq!(
            c::oracle_audio_mix_input_resampled(co, 0, 0),
            (*ra).mix_input_resampled_for_test(0, 0)
        );
        let cc = &*c::oracle_audio_mix_input_conversion(co, 0, 0);
        let rc = (*ra).mix_input_conversion_for_test(0, 0);
        assert_eq!(cc.samples_per_sec, rc.samples_per_sec);
        assert_eq!(cc.format, rc.format);
        assert_eq!(cc.speakers, rc.speakers);
        assert_eq!(cc.allow_clipping, rc.allow_clipping);
        // Disconnect restores the inactive state on both.
        c::oracle_audio_output_disconnect(co, 0, Some(output_c), ptr::null_mut());
        rs::audio_output_disconnect(ro, 0, Some(output_rs), ptr::null_mut());
        assert_eq!(
            c::oracle_audio_output_active(co),
            rs::audio_output_active(ro)
        );
        close_pair(co, ro);
    }
}

#[test]
fn tick_is_identical() {
    let _drive = DRIVER.lock().unwrap_or_else(|e| e.into_inner());
    // Real input callbacks everywhere; only explicit MAGIC-timed ticks are
    // recorded, so the real threads' asynchronous ticks are ignored.
    unsafe {
        let mut co: *mut c::OracleAudioOutput = ptr::null_mut();
        let mut info = info_c(None);
        info.input_callback = Some(input_c);
        assert_eq!(c::oracle_audio_output_open_no_thread(&mut co, &mut info), 0);
        let ri = info_rs(Some(input_rs));
        let ro = Box::into_raw(AudioOutput::open_no_worker(&ri).expect("rust open")) as *mut c_void;
        let mut tag: usize = 0;
        c::oracle_audio_output_connect(
            co,
            0,
            ptr::null(),
            Some(output_c),
            (&mut tag) as *mut _ as _,
        );
        rs::audio_output_connect(ro, 0, ptr::null(), Some(output_rs), ptr::null_mut());
        c::oracle_audio_output_connect(co, 0, ptr::null(), Some(output_c2), ptr::null_mut());
        rs::audio_output_connect(ro, 0, ptr::null(), Some(output_rs2), ptr::null_mut());

        let ra = ro as *mut AudioOutput;
        c::oracle_audio_tick(co, MAGIC + 21_000_000, MAGIC);
        let c_tick = take_tick();
        (*ra).tick_for_test(MAGIC + 21_000_000, MAGIC);
        let r_tick = take_tick();
        assert_eq!(c_tick, r_tick);
        assert!(c_tick.is_some(), "tick produced a record");

        // After disconnecting both mixes' inputs, a tick reports no mixes.
        c::oracle_audio_output_disconnect(co, 0, Some(output_c), (&mut tag) as *mut _ as _);
        rs::audio_output_disconnect(ro, 0, Some(output_rs), ptr::null_mut());
        c::oracle_audio_output_disconnect(co, 0, Some(output_c2), ptr::null_mut());
        rs::audio_output_disconnect(ro, 0, Some(output_rs2), ptr::null_mut());
        c::oracle_audio_tick(co, MAGIC + 42_000_000, MAGIC);
        let c_tick = take_tick();
        (*ra).tick_for_test(MAGIC + 42_000_000, MAGIC);
        assert_eq!(c_tick, take_tick());

        close_pair(co, ro);
    }
}

// Second callbacks so reverse-order iteration shows up as (idx, source).
unsafe extern "C" fn output_rs2(_p: *mut c_void, mix_idx: usize, data: *mut rs::audio_data) {
    let d = unsafe { &*data };
    record_output(mix_idx, d.timestamp, d.frames + 1, d.data[0] as *const f32); // +1 marker
}

unsafe extern "C" fn output_c2(_p: *mut c_void, mix_idx: usize, data: *mut c::OracleAudioData) {
    let d = unsafe { &*data };
    record_output(mix_idx, d.timestamp, d.frames + 1, d.data[0] as *const f32);
}

/// Marker-writing input callbacks: the marker f32 bits travel in
/// `input_param`, so a proptest case can drive any sample value — including
/// out-of-range and NaN — through the timestamp/clamp path.
unsafe extern "C" fn input_marked_rs(
    param: *mut c_void,
    start: u64,
    end: u64,
    new_ts: *mut u64,
    active: u32,
    mixes: *mut rs::audio_output_data,
) -> bool {
    if start == MAGIC {
        record_input(start, end, new_ts, active);
        unsafe { *(*mixes).data[0] = f32::from_bits(param as u64 as u32) };
    }
    true
}

unsafe extern "C" fn input_marked_c(
    param: *mut c_void,
    start: u64,
    end: u64,
    new_ts: *mut u64,
    active: u32,
    mixes: *mut c::OracleAudioOutputData,
) -> bool {
    if start == MAGIC {
        record_input(start, end, new_ts, active);
        unsafe { *(*mixes).data[0] = f32::from_bits(param as u64 as u32) };
    }
    true
}

/// Full-frame output callbacks: compare every sample, not just the first.
unsafe extern "C" fn output_full_rs(_p: *mut c_void, mix_idx: usize, data: *mut rs::audio_data) {
    let d = unsafe { &*data };
    if !is_magic_ts(d.timestamp) {
        return;
    }
    let mut acc = 0u32;
    for i in 0..d.frames as usize {
        acc = acc
            .wrapping_mul(31)
            .wrapping_add(unsafe { *d.data[0].cast::<f32>().add(i) }.to_bits());
    }
    let mut g = TICK.lock().unwrap();
    if let Some(t) = g.as_mut() {
        t.calls.push((mix_idx, acc, 0));
    }
}

unsafe extern "C" fn output_full_c(_p: *mut c_void, mix_idx: usize, data: *mut c::OracleAudioData) {
    let d = unsafe { &*data };
    if !is_magic_ts(d.timestamp) {
        return;
    }
    let mut acc = 0u32;
    for i in 0..d.frames as usize {
        acc = acc
            .wrapping_mul(31)
            .wrapping_add(unsafe { *d.data[0].cast::<f32>().add(i) }.to_bits());
    }
    let mut g = TICK.lock().unwrap();
    if let Some(t) = g.as_mut() {
        t.calls.push((mix_idx, acc, 0));
    }
}

proptest::proptest! {
    /// Varied timestamp deltas, marker sample values (including out-of-range
    /// and NaN through the clamp path), and 1-3 inputs across mixes — the
    /// whole call sequence must be bit-identical between oracle and port.
    /// Resampling-on is unreachable here: the oracle stubs
    /// `audio_resampler_create` to NULL, so differing conversions refuse on
    /// both sides (covered in connect_disconnect_match).
    #[test]
    fn tick_sequence_matches(
        marker_bits in prop_oneof![
            Just(0.5f32.to_bits()),
            Just(2.5f32.to_bits()),
            Just(f32::NAN.to_bits()),
            Just((-2.5f32).to_bits()),
        ],
        prev_delta in 0u64..50_000_000u64,
        n_inputs in 1usize..=3,
        clip_flags in prop::collection::vec(any::<bool>(), 3),
    ) {
        let _drive = DRIVER.lock().unwrap_or_else(|e| e.into_inner());
        unsafe {
            let marker = marker_bits as usize as *mut c_void;
            let mut co: *mut c::OracleAudioOutput = ptr::null_mut();
            let mut ci = info_c(None);
            ci.input_callback = Some(input_marked_c);
            ci.input_param = marker;
            assert_eq!(c::oracle_audio_output_open_no_thread(&mut co, &mut ci), 0);
            let mut ri = info_rs(Some(input_marked_rs));
            ri.input_param = marker;
            let ro = Box::into_raw(AudioOutput::open_no_worker(&ri).expect("rust open")) as *mut c_void;

            for (i, clip) in clip_flags.iter().enumerate().take(n_inputs) {
                if *clip {
                    let conv = rs::audio_convert_info {
                        samples_per_sec: 0,
                        format: 0,
                        speakers: 0,
                        allow_clipping: true,
                    };
                    let cconv = c::OracleAudioConvertInfo {
                        samples_per_sec: 0,
                        format: 0,
                        speakers: 0,
                        allow_clipping: true,
                    };
                    let cparam = i as *mut c_void;
                    assert_eq!(
                        c::oracle_audio_output_connect(co, i, &cconv, Some(output_full_c), cparam),
                        rs::audio_output_connect(ro, i, &conv, Some(output_full_rs), cparam),
                    );
                } else {
                    let cparam = i as *mut c_void;
                    assert_eq!(
                        c::oracle_audio_output_connect(co, i, ptr::null(), Some(output_full_c), cparam),
                        rs::audio_output_connect(ro, i, ptr::null(), Some(output_full_rs), cparam),
                    );
                }
            }

            let ra = ro as *mut AudioOutput;
            let end = MAGIC.wrapping_add(prev_delta.wrapping_add(1));
            c::oracle_audio_tick(co, end, MAGIC);
            let c_tick = take_tick();
            (*ra).tick_for_test(end, MAGIC);
            let r_tick = take_tick();
            assert_eq!(c_tick, r_tick, "marker={marker_bits:#x} delta={prev_delta} n={n_inputs}");

            close_pair(co, ro);
        }
    }
}

/* ------------------------------------------------------------------ */
/* Reentrancy regressions (fork PR #35 review acceptance): the same     */
/* callback program runs on the oracle and on the port, and the        */
/* recorded order must be identical. Each ctx carries the handle the    */
/* re-entrant callback needs; everything is gated on is_magic_ts so    */
/* stray ticks can never fire these paths.                             */

// --- fresh clamp snapshot (item 2) ------------------------------------
// The C's clamp pass checks `!mix->inputs.num` AFTER the input callback —
// a mix that gained an input mid-callback is clamped anyway. A port that
// reuses the pre-callback mask skips it: its buffer_unclamped stays zeroed
// while the C's copies the marker the input cb wrote.

// The two mix-1 inputs MUST keep distinct function pointers: the C
// refuses a duplicate (callback, param) per mix, and under release the
// linker merges byte-identical function bodies (ICF), which would make
// `fs_clamped_* == fs_new_*` and drop the second connect. The black_box
// immediates keep each body distinct; they record identically.
unsafe extern "C" fn fs_new_c(_p: *mut c_void, m: usize, d: *mut c::OracleAudioData) {
    std::hint::black_box(0u8);
    let d = unsafe { &*d };
    record_output(m, d.timestamp, d.frames, d.data[0] as *const f32);
}
unsafe extern "C" fn fs_new_rs(_p: *mut c_void, m: usize, d: *mut rs::audio_data) {
    std::hint::black_box(0u8);
    let d = unsafe { &*d };
    record_output(m, d.timestamp, d.frames, d.data[0] as *const f32);
}
// A clamped sibling on the same mid-callback-connected mix: it must see
// the [-1,1] clamped buffer where the allow_clipping input sees raw 2.5.
unsafe extern "C" fn fs_clamped_c(_p: *mut c_void, m: usize, d: *mut c::OracleAudioData) {
    std::hint::black_box(1u8);
    let d = unsafe { &*d };
    record_output(m, d.timestamp, d.frames, d.data[0] as *const f32);
}
unsafe extern "C" fn fs_clamped_rs(_p: *mut c_void, m: usize, d: *mut rs::audio_data) {
    std::hint::black_box(1u8);
    let d = unsafe { &*d };
    record_output(m, d.timestamp, d.frames, d.data[0] as *const f32);
}

// Connect an allow_clipping input to the (initially empty) mix 1 during
// the input callback, and write an out-of-range marker into its buffer.
// Same (callback, param) on a later tick is refused as a duplicate, so
// repeated ticks stay deterministic without a one-shot flag.
unsafe extern "C" fn fs_input_c(
    p: *mut c_void,
    start: u64,
    end: u64,
    new_ts: *mut u64,
    active: u32,
    mixes: *mut c::OracleAudioOutputData,
) -> bool {
    if start == MAGIC {
        record_input(start, end, new_ts, active);
        let ctx = unsafe { &*(p as *const ReCtx) };
        unsafe { *(*mixes.add(1)).data[0].cast::<f32>() = 2.5f32 };
        let conv = c::OracleAudioConvertInfo {
            samples_per_sec: 0,
            format: 0,
            speakers: 0,
            allow_clipping: true,
        };
        unsafe {
            c::oracle_audio_output_connect(ctx.audio as _, 1, &conv, Some(fs_new_c), ctx.extra)
        };
        let conv_clamped = c::OracleAudioConvertInfo {
            samples_per_sec: 0,
            format: 0,
            speakers: 0,
            allow_clipping: false,
        };
        unsafe {
            c::oracle_audio_output_connect(
                ctx.audio as _,
                1,
                &conv_clamped,
                Some(fs_clamped_c),
                ctx.extra,
            )
        };
    }
    true
}
unsafe extern "C" fn fs_input_rs(
    p: *mut c_void,
    start: u64,
    end: u64,
    new_ts: *mut u64,
    active: u32,
    mixes: *mut rs::audio_output_data,
) -> bool {
    if start == MAGIC {
        record_input(start, end, new_ts, active);
        let ctx = unsafe { &*(p as *const ReCtx) };
        unsafe { *(*mixes.add(1)).data[0].cast::<f32>() = 2.5f32 };
        let conv = rs::audio_convert_info {
            samples_per_sec: 0,
            format: 0,
            speakers: 0,
            allow_clipping: true,
        };
        unsafe { rs::audio_output_connect(ctx.audio, 1, &conv, Some(fs_new_rs), ctx.extra) };
        let conv_clamped = rs::audio_convert_info {
            samples_per_sec: 0,
            format: 0,
            speakers: 0,
            allow_clipping: false,
        };
        unsafe {
            rs::audio_output_connect(ctx.audio, 1, &conv_clamped, Some(fs_clamped_rs), ctx.extra)
        };
    }
    true
}

/// The input callback needs the opened handle in its param: box the ctx,
/// point `input_param` at the stable box, then fill `.audio` post-open.
#[test]
fn fresh_clamp_snapshot_parity() {
    let _drive = DRIVER.lock().unwrap_or_else(|e| e.into_inner());
    unsafe {
        let mut cctx = Box::new(ReCtx {
            audio: ptr::null_mut(),
            extra: 0x9usize as *mut c_void,
        });
        let mut ci = info_c(None);
        ci.input_callback = Some(fs_input_c);
        ci.input_param = &*cctx as *const ReCtx as *mut c_void;
        let mut co: *mut c::OracleAudioOutput = ptr::null_mut();
        assert_eq!(c::oracle_audio_output_open_no_thread(&mut co, &mut ci), 0);
        cctx.audio = co as *mut c_void;

        let mut rctx = Box::new(ReCtx {
            audio: ptr::null_mut(),
            extra: 0x9usize as *mut c_void,
        });
        let mut ri = info_rs(Some(fs_input_rs));
        ri.input_param = &*rctx as *const ReCtx as *mut c_void;
        let ro = Box::into_raw(AudioOutput::open_no_worker(&ri).expect("rust open")) as *mut c_void;
        rctx.audio = ro;

        // One ordinary input on mix 0 so active_mixes is non-zero.
        assert!(c::oracle_audio_output_connect(
            co,
            0,
            ptr::null(),
            Some(fs_new_c),
            ptr::null_mut()
        ));
        assert!(rs::audio_output_connect(
            ro,
            0,
            ptr::null(),
            Some(fs_new_rs),
            ptr::null_mut()
        ));

        let ra = ro as *mut AudioOutput;
        c::oracle_audio_tick(co, MAGIC + 21_000_000, MAGIC);
        let c_tick = take_tick();
        (*ra).tick_for_test(MAGIC + 21_000_000, MAGIC);
        let r_tick = take_tick();
        assert_eq!(c_tick, r_tick);
        // Mix 1's inputs joined after the mask snapshot; the C still
        // clamps it, so the allow_clipping row carries raw 2.5 while its
        // clamped sibling sees 1.0 — and the recorded mask keeps only the
        // originally-active mix 0.
        assert!(c_tick.is_some_and(|t| {
            t.calls
                .iter()
                .any(|&(m, _, bits)| m == 1 && bits == 2.5f32.to_bits())
                && t.calls
                    .iter()
                    .any(|&(m, _, bits)| m == 1 && bits == 1.0f32.to_bits())
                && t.active_mixes == 0b1
        }));
        close_pair(co, ro);
        // Contexts outlive close: their param pointers are what callbacks
        // dereferenced.
        let _ = (cctx, rctx);
    }
}

// --- real resampling parity (not NULL-resampler stubs) ------------------
// The oracle supplies a deterministic resampler, so a conversion that
// differs only in rate creates one on both sides and the resample path —
// swapped data pointers, shortened frame count — is exercised end to end.

#[test]
fn resample_parity() {
    let _drive = DRIVER.lock().unwrap_or_else(|e| e.into_inner());
    unsafe {
        let (co, ro) = open_pair();
        let conv = rs::audio_convert_info {
            samples_per_sec: 24000,
            format: FORMAT_FLOAT,
            speakers: SPEAKERS_STEREO,
            allow_clipping: false,
        };
        let cconv = c::OracleAudioConvertInfo {
            samples_per_sec: 24000,
            format: FORMAT_FLOAT,
            speakers: SPEAKERS_STEREO,
            allow_clipping: false,
        };
        // output_c/output_rs record data->frames: a resampled input sees
        // 256 (the NULL stub left it at the output's 512).
        assert!(c::oracle_audio_output_connect(
            co,
            0,
            &cconv,
            Some(output_c),
            ptr::null_mut()
        ));
        assert!(rs::audio_output_connect(
            ro,
            0,
            &conv,
            Some(output_rs),
            ptr::null_mut()
        ));
        let ra = ro as *mut AudioOutput;
        assert_eq!(
            c::oracle_audio_mix_input_resampled(co, 0, 0),
            (*ra).mix_input_resampled_for_test(0, 0)
        );
        assert!(c::oracle_audio_mix_input_resampled(co, 0, 0));

        c::oracle_audio_tick(co, MAGIC + 21_000_000, MAGIC);
        let c_tick = take_tick();
        (*ra).tick_for_test(MAGIC + 21_000_000, MAGIC);
        let r_tick = take_tick();
        assert_eq!(c_tick, r_tick);
        // 1024 frames at 48k -> 512 at 24k: resampling ran on both sides
        // (a same-rate input would hand the callback all 1024 frames; the
        // NULL stub made the connect fail entirely).
        assert!(c_tick.is_some_and(|t| t.calls.iter().any(|&(_, f, _)| f == 512)));
        close_pair(co, ro);
    }
}

/// Failure cases, kept separate from the rate-only success case: the
/// oracle's resampler double is rate-only — a format or speaker-layout
/// conversion is rejected at resampler create, so connect fails
/// identically on both sides and the mix stays empty.
#[test]
fn resample_failure_parity() {
    let _drive = DRIVER.lock().unwrap_or_else(|e| e.into_inner());
    unsafe {
        let (co, ro) = open_pair();
        for (rate, format, speakers) in [
            (24000u32, 8, SPEAKERS_STEREO), // format differs (planar f32)
            (24000u32, FORMAT_FLOAT, 4),    // speaker layout differs
            (48000u32, 8, 4),               // both differ
        ] {
            let conv = rs::audio_convert_info {
                samples_per_sec: rate,
                format,
                speakers,
                allow_clipping: false,
            };
            let cconv = c::OracleAudioConvertInfo {
                samples_per_sec: rate,
                format,
                speakers,
                allow_clipping: false,
            };
            let c_ok =
                c::oracle_audio_output_connect(co, 0, &cconv, Some(output_c), ptr::null_mut());
            let r_ok = rs::audio_output_connect(ro, 0, &conv, Some(output_rs), ptr::null_mut());
            assert_eq!(
                c_ok, r_ok,
                "rate={rate} format={format} speakers={speakers}"
            );
            assert!(!c_ok);
        }
        let ra = ro as *mut AudioOutput;
        assert_eq!(
            c::oracle_audio_mix_input_count(co, 0),
            (*ra).mix_input_count_for_test(0)
        );
        assert_eq!(c::oracle_audio_mix_input_count(co, 0), 0);
        close_pair(co, ro);
    }
}

/// Timestamp+frames recorder for the ts-offset case: unlike
/// `record_output` it keeps `d.timestamp`, which the resample path shifts
/// by the reported offset.
static TS_LOG: Mutex<Vec<(usize, u32, u64)>> = Mutex::new(Vec::new());

fn take_ts_log() -> Vec<(usize, u32, u64)> {
    std::mem::take(&mut *TS_LOG.lock().unwrap())
}

unsafe extern "C" fn ts_output_rs(_p: *mut c_void, mix_idx: usize, data: *mut rs::audio_data) {
    let d = unsafe { &*data };
    TS_LOG
        .lock()
        .unwrap()
        .push((mix_idx, d.frames, d.timestamp));
}
unsafe extern "C" fn ts_output_c(_p: *mut c_void, mix_idx: usize, data: *mut c::OracleAudioData) {
    let d = unsafe { &*data };
    TS_LOG
        .lock()
        .unwrap()
        .push((mix_idx, d.frames, d.timestamp));
}

/// Nonzero timestamp-offset case, separate from the rate-only success
/// case: a non-integer rate ratio makes the double's carried remainder
/// report a nonzero ts_offset, exercising `data.timestamp -= offset`.
#[test]
fn resample_ts_offset_parity() {
    let _drive = DRIVER.lock().unwrap_or_else(|e| e.into_inner());
    unsafe {
        let (co, ro) = open_pair();
        // 48k mix buffer resampled into a 44.1k input: 1024 in -> 940 out + residue.
        let conv = rs::audio_convert_info {
            samples_per_sec: 44100,
            format: FORMAT_FLOAT,
            speakers: SPEAKERS_STEREO,
            allow_clipping: false,
        };
        let cconv = c::OracleAudioConvertInfo {
            samples_per_sec: 44100,
            format: FORMAT_FLOAT,
            speakers: SPEAKERS_STEREO,
            allow_clipping: false,
        };
        assert!(c::oracle_audio_output_connect(
            co,
            0,
            &cconv,
            Some(ts_output_c),
            ptr::null_mut()
        ));
        assert!(rs::audio_output_connect(
            ro,
            0,
            &conv,
            Some(ts_output_rs),
            ptr::null_mut()
        ));
        let ra = ro as *mut AudioOutput;

        c::oracle_audio_tick(co, MAGIC + 21_000_000, MAGIC);
        let c_log = take_ts_log();
        (*ra).tick_for_test(MAGIC + 21_000_000, MAGIC);
        let r_log = take_ts_log();
        assert_eq!(c_log, r_log);
        // 1024*44100/48000 = 940.8 -> 940 frames; residue 38400 gives
        // ts_offset = 38400e9/(44100*48000) = 18140 ns shifted off new_ts.
        assert_eq!(c_log, [(0usize, 940u32, MAGIC + 21_000_000 - 18140)]);
        close_pair(co, ro);
    }
}

/// Handle the re-entrant callbacks operate on (oracle or rust audio_t).
struct ReCtx {
    audio: *mut c_void,
    extra: *mut c_void,
}

// --- activation during input ---------------------------------------------

/// The mask the input callback already received and the tags the
/// activated input pushed, in order.
static AD_MASK: Mutex<Vec<u32>> = Mutex::new(Vec::new());
static AD_C_ARMED: AtomicUsize = AtomicUsize::new(0);
static AD_RS_ARMED: AtomicUsize = AtomicUsize::new(0);

fn take_ad_mask() -> Vec<u32> {
    std::mem::take(&mut *AD_MASK.lock().unwrap())
}

unsafe extern "C" fn ad_base_c(_p: *mut c_void, _m: usize, d: *mut c::OracleAudioData) {
    if is_magic_ts(unsafe { *d }.timestamp) {
        push_order(1);
    }
}
unsafe extern "C" fn ad_base_rs(_p: *mut c_void, _m: usize, d: *mut rs::audio_data) {
    if is_magic_ts(unsafe { *d }.timestamp) {
        push_order(1);
    }
}
unsafe extern "C" fn ad_new_c(_p: *mut c_void, _m: usize, d: *mut c::OracleAudioData) {
    if is_magic_ts(unsafe { *d }.timestamp) {
        push_order(9);
    }
}
unsafe extern "C" fn ad_new_rs(_p: *mut c_void, _m: usize, d: *mut rs::audio_data) {
    if is_magic_ts(unsafe { *d }.timestamp) {
        push_order(9);
    }
}

unsafe extern "C" fn ad_input_c(
    p: *mut c_void,
    start: u64,
    end: u64,
    new_ts: *mut u64,
    active: u32,
    _mixes: *mut c::OracleAudioOutputData,
) -> bool {
    if start != MAGIC {
        return true;
    }
    AD_MASK.lock().unwrap().push(active);
    unsafe { *new_ts = end };
    let ctx = unsafe { &*(p as *const ReCtx) };
    if AD_C_ARMED.swap(1, Ordering::SeqCst) == 0 {
        unsafe {
            c::oracle_audio_output_connect(
                ctx.audio as _,
                1,
                ptr::null(),
                Some(ad_new_c),
                ctx.extra,
            )
        };
    }
    true
}

unsafe extern "C" fn ad_input_rs(
    p: *mut c_void,
    start: u64,
    end: u64,
    new_ts: *mut u64,
    active: u32,
    _mixes: *mut rs::audio_output_data,
) -> bool {
    if start != MAGIC {
        return true;
    }
    AD_MASK.lock().unwrap().push(active);
    unsafe { *new_ts = end };
    let ctx = unsafe { &*(p as *const ReCtx) };
    if AD_RS_ARMED.swap(1, Ordering::SeqCst) == 0 {
        unsafe { rs::audio_output_connect(ctx.audio, 1, ptr::null(), Some(ad_new_rs), ctx.extra) };
    }
    true
}

/// A connect issued inside the input callback activates an input on this
/// same tick — the dispatch re-reads the registry after `input_cb`
/// returns — while the `active` mask that call already received predates
/// the connect. The C and the port must agree on both halves.
#[test]
fn activation_during_input_parity() {
    let _drive = DRIVER.lock().unwrap_or_else(|e| e.into_inner());
    unsafe {
        let mut cctx = ReCtx {
            audio: ptr::null_mut(),
            extra: ptr::null_mut(),
        };
        let mut rctx = ReCtx {
            audio: ptr::null_mut(),
            extra: ptr::null_mut(),
        };
        let mut co: *mut c::OracleAudioOutput = ptr::null_mut();
        let mut cinfo = info_c(None);
        cinfo.input_callback = Some(ad_input_c);
        cinfo.input_param = &cctx as *const ReCtx as *mut c_void;
        assert_eq!(
            c::oracle_audio_output_open_no_thread(&mut co, &mut cinfo),
            0
        );
        let mut rinfo = info_rs(Some(ad_input_rs));
        rinfo.input_param = &rctx as *const ReCtx as *mut c_void;
        let ro = Box::into_raw(AudioOutput::open_no_worker(&rinfo).unwrap()) as *mut c_void;
        let ra = ro as *mut AudioOutput;
        cctx.audio = co as *mut c_void;
        rctx.audio = ro;
        assert!(!cctx.audio.is_null() && !rctx.audio.is_null());

        assert!(c::oracle_audio_output_connect(
            co,
            0,
            ptr::null(),
            Some(ad_base_c),
            ptr::null_mut()
        ));
        assert!(rs::audio_output_connect(
            ro,
            0,
            ptr::null(),
            Some(ad_base_rs),
            ptr::null_mut()
        ));

        AD_C_ARMED.store(0, Ordering::SeqCst);
        AD_RS_ARMED.store(0, Ordering::SeqCst);
        // Tick 1: mask saw only mix 0, yet the just-connected mix-1 input
        // still dispatches on this same tick.
        take_ad_mask();
        take_order();
        c::oracle_audio_tick(co, MAGIC + 21_000_000, MAGIC);
        let c_order = take_order();
        let c_mask = take_ad_mask();
        take_ad_mask();
        take_order();
        (*ra).tick_for_test(MAGIC + 21_000_000, MAGIC);
        let r_order = take_order();
        let r_mask = take_ad_mask();
        assert_eq!(c_order, r_order);
        assert_eq!(c_mask, r_mask);
        assert_eq!(c_order, [1, 9]);
        assert_eq!(c_mask, [1]);

        // Tick 2: the new input stays connected; now the mask lists both.
        take_order();
        take_ad_mask();
        c::oracle_audio_tick(co, MAGIC + 31_000_000, MAGIC);
        let c_order = take_order();
        let c_mask = take_ad_mask();
        take_order();
        take_ad_mask();
        (*ra).tick_for_test(MAGIC + 31_000_000, MAGIC);
        let r_order = take_order();
        let r_mask = take_ad_mask();
        assert_eq!(c_order, r_order);
        assert_eq!(c_mask, r_mask);
        assert_eq!(c_order, [1, 9]);
        assert_eq!(c_mask, [3]);

        close_pair(co, ro);
    }
}

// --- self-disconnect -------------------------------------------------

unsafe extern "C" fn sd_other_c(_p: *mut c_void, _m: usize, d: *mut c::OracleAudioData) {
    if is_magic_ts(unsafe { *d }.timestamp) {
        push_order(1);
    }
}
unsafe extern "C" fn sd_other_rs(_p: *mut c_void, _m: usize, d: *mut rs::audio_data) {
    if is_magic_ts(unsafe { *d }.timestamp) {
        push_order(1);
    }
}
unsafe extern "C" fn sd_self_c(p: *mut c_void, m: usize, d: *mut c::OracleAudioData) {
    if !is_magic_ts(unsafe { *d }.timestamp) {
        return;
    }
    push_order(2);
    let ctx = unsafe { &*(p as *const ReCtx) };
    unsafe { c::oracle_audio_output_disconnect(ctx.audio as _, m, Some(sd_self_c), p) };
}
unsafe extern "C" fn sd_self_rs(p: *mut c_void, m: usize, d: *mut rs::audio_data) {
    if !is_magic_ts(unsafe { *d }.timestamp) {
        return;
    }
    push_order(2);
    let ctx = unsafe { &*(p as *const ReCtx) };
    unsafe { rs::audio_output_disconnect(ctx.audio, m, Some(sd_self_rs), p) };
}

/// An output callback disconnects its own input mid-dispatch: the C's
/// recursive input_mutex exists for this; the walk continues at the next
/// index and must not corrupt or re-run the remaining input.
#[test]
fn self_disconnect_parity() {
    let _drive = DRIVER.lock().unwrap_or_else(|e| e.into_inner());
    unsafe {
        let (co, ro) = open_pair();
        let cctx = ReCtx {
            audio: co as *mut c_void,
            extra: ptr::null_mut(),
        };
        let rctx = ReCtx {
            audio: ro,
            extra: ptr::null_mut(),
        };
        let cp = &cctx as *const ReCtx as *mut c_void;
        let rp = &rctx as *const ReCtx as *mut c_void;
        assert!(c::oracle_audio_output_connect(
            co,
            0,
            ptr::null(),
            Some(sd_other_c),
            ptr::null_mut()
        ));
        assert!(rs::audio_output_connect(
            ro,
            0,
            ptr::null(),
            Some(sd_other_rs),
            ptr::null_mut()
        ));
        assert!(c::oracle_audio_output_connect(
            co,
            0,
            ptr::null(),
            Some(sd_self_c),
            cp
        ));
        assert!(rs::audio_output_connect(
            ro,
            0,
            ptr::null(),
            Some(sd_self_rs),
            rp
        ));

        c::oracle_audio_tick(co, MAGIC + 21_000_000, MAGIC);
        let c_order = take_order();
        let ra = ro as *mut AudioOutput;
        (*ra).tick_for_test(MAGIC + 21_000_000, MAGIC);
        let r_order = take_order();
        assert_eq!(c_order, r_order);
        assert_eq!(c_order, [2, 1]);
        assert_eq!(
            c::oracle_audio_mix_input_count(co, 0),
            (*ra).mix_input_count_for_test(0)
        );
        // Only the self-disconnecting input is gone; `other` survives.
        assert_eq!(c::oracle_audio_mix_input_count(co, 0), 1);
        close_pair(co, ro);
    }
}

// --- earlier-input removal ----------------------------------------------

static A_TAG: u8 = 0;

unsafe extern "C" fn rm_a_c(_p: *mut c_void, _m: usize, d: *mut c::OracleAudioData) {
    if is_magic_ts(unsafe { *d }.timestamp) {
        push_order(1);
    }
}
unsafe extern "C" fn rm_a_rs(_p: *mut c_void, _m: usize, d: *mut rs::audio_data) {
    if is_magic_ts(unsafe { *d }.timestamp) {
        push_order(1);
    }
}
unsafe extern "C" fn rm_c_c(_p: *mut c_void, _m: usize, d: *mut c::OracleAudioData) {
    if is_magic_ts(unsafe { *d }.timestamp) {
        push_order(3);
    }
}
unsafe extern "C" fn rm_c_rs(_p: *mut c_void, _m: usize, d: *mut rs::audio_data) {
    if is_magic_ts(unsafe { *d }.timestamp) {
        push_order(3);
    }
}
unsafe extern "C" fn rm_b_c(p: *mut c_void, _m: usize, d: *mut c::OracleAudioData) {
    if !is_magic_ts(unsafe { *d }.timestamp) {
        return;
    }
    push_order(2);
    let ctx = unsafe { &*(p as *const ReCtx) };
    unsafe { c::oracle_audio_output_disconnect(ctx.audio as _, 0, Some(rm_a_c), ctx.extra) };
}
unsafe extern "C" fn rm_b_rs(p: *mut c_void, _m: usize, d: *mut rs::audio_data) {
    if !is_magic_ts(unsafe { *d }.timestamp) {
        return;
    }
    push_order(2);
    let ctx = unsafe { &*(p as *const ReCtx) };
    unsafe { rs::audio_output_disconnect(ctx.audio, 0, Some(rm_a_rs), ctx.extra) };
}

/// A callback removes an EARLIER input: the shifted tail element slides
/// into the not-yet-visited index and is invoked a second time — the C
/// re-reads array[i-1] each iteration, so [A,B,C] with B removing A
/// produces C,B,B. The port must reproduce this index-for-index.
#[test]
fn earlier_input_removal_parity() {
    let _drive = DRIVER.lock().unwrap_or_else(|e| e.into_inner());
    unsafe {
        let (co, ro) = open_pair();
        let a_param = &A_TAG as *const u8 as *mut c_void;
        let cctx = ReCtx {
            audio: co as *mut c_void,
            extra: a_param,
        };
        let rctx = ReCtx {
            audio: ro,
            extra: a_param,
        };
        let cp = &cctx as *const ReCtx as *mut c_void;
        let rp = &rctx as *const ReCtx as *mut c_void;
        assert!(c::oracle_audio_output_connect(
            co,
            0,
            ptr::null(),
            Some(rm_a_c),
            a_param
        ));
        assert!(rs::audio_output_connect(
            ro,
            0,
            ptr::null(),
            Some(rm_a_rs),
            a_param
        ));
        assert!(c::oracle_audio_output_connect(
            co,
            0,
            ptr::null(),
            Some(rm_b_c),
            cp
        ));
        assert!(rs::audio_output_connect(
            ro,
            0,
            ptr::null(),
            Some(rm_b_rs),
            rp
        ));
        assert!(c::oracle_audio_output_connect(
            co,
            0,
            ptr::null(),
            Some(rm_c_c),
            ptr::null_mut()
        ));
        assert!(rs::audio_output_connect(
            ro,
            0,
            ptr::null(),
            Some(rm_c_rs),
            ptr::null_mut()
        ));

        c::oracle_audio_tick(co, MAGIC + 21_000_000, MAGIC);
        let c_order = take_order();
        let ra = ro as *mut AudioOutput;
        (*ra).tick_for_test(MAGIC + 21_000_000, MAGIC);
        let r_order = take_order();
        assert_eq!(c_order, r_order);
        assert_eq!(c_order, [3, 2, 2]);
        assert_eq!(
            c::oracle_audio_mix_input_count(co, 0),
            (*ra).mix_input_count_for_test(0)
        );
        close_pair(co, ro);
    }
}

// --- connect grows the Vec mid-dispatch -----------------------------------

unsafe extern "C" fn gr_a_c(_p: *mut c_void, _m: usize, d: *mut c::OracleAudioData) {
    if is_magic_ts(unsafe { *d }.timestamp) {
        push_order(1);
    }
}
unsafe extern "C" fn gr_a_rs(_p: *mut c_void, _m: usize, d: *mut rs::audio_data) {
    if is_magic_ts(unsafe { *d }.timestamp) {
        push_order(1);
    }
}
unsafe extern "C" fn gr_b_c(_p: *mut c_void, _m: usize, d: *mut c::OracleAudioData) {
    if is_magic_ts(unsafe { *d }.timestamp) {
        push_order(2);
    }
}
unsafe extern "C" fn gr_b_rs(_p: *mut c_void, _m: usize, d: *mut rs::audio_data) {
    if is_magic_ts(unsafe { *d }.timestamp) {
        push_order(2);
    }
}
unsafe extern "C" fn gr_c_c(_p: *mut c_void, _m: usize, d: *mut c::OracleAudioData) {
    if is_magic_ts(unsafe { *d }.timestamp) {
        push_order(3);
    }
}
unsafe extern "C" fn gr_c_rs(_p: *mut c_void, _m: usize, d: *mut rs::audio_data) {
    if is_magic_ts(unsafe { *d }.timestamp) {
        push_order(3);
    }
}
unsafe extern "C" fn gr_e_c(_p: *mut c_void, _m: usize, d: *mut c::OracleAudioData) {
    if is_magic_ts(unsafe { *d }.timestamp) {
        push_order(5);
    }
}
unsafe extern "C" fn gr_e_rs(_p: *mut c_void, _m: usize, d: *mut rs::audio_data) {
    if is_magic_ts(unsafe { *d }.timestamp) {
        push_order(5);
    }
}
unsafe extern "C" fn gr_d_c(p: *mut c_void, _m: usize, d: *mut c::OracleAudioData) {
    if !is_magic_ts(unsafe { *d }.timestamp) {
        return;
    }
    push_order(4);
    let ctx = unsafe { &*(p as *const ReCtx) };
    unsafe {
        c::oracle_audio_output_connect(ctx.audio as _, 0, ptr::null(), Some(gr_e_c), ctx.extra)
    };
}
unsafe extern "C" fn gr_d_rs(p: *mut c_void, _m: usize, d: *mut rs::audio_data) {
    if !is_magic_ts(unsafe { *d }.timestamp) {
        return;
    }
    push_order(4);
    let ctx = unsafe { &*(p as *const ReCtx) };
    unsafe { rs::audio_output_connect(ctx.audio, 0, ptr::null(), Some(gr_e_rs), ctx.extra) };
}

/// A callback connects a new input while the mix already holds four —
/// pushing reallocs the Vec mid-dispatch. The C's fixed loop bound means
/// the appended input is not called this tick; first-tick order is
/// D,C,B,A, second tick E,D,C,B,A (reconnect refused as a duplicate).
#[test]
fn vec_growth_parity() {
    let _drive = DRIVER.lock().unwrap_or_else(|e| e.into_inner());
    unsafe {
        let (co, ro) = open_pair();
        let e_param = 0xe as *mut c_void;
        let cctx = ReCtx {
            audio: co as *mut c_void,
            extra: e_param,
        };
        let rctx = ReCtx {
            audio: ro,
            extra: e_param,
        };
        let cp = &cctx as *const ReCtx as *mut c_void;
        let rp = &rctx as *const ReCtx as *mut c_void;
        for (cb_c, cb_rs) in [
            (gr_a_c as _, gr_a_rs as _),
            (gr_b_c as _, gr_b_rs as _),
            (gr_c_c as _, gr_c_rs as _),
        ] {
            let cb_c: c::OracleAudioOutputCallback = Some(cb_c);
            let cb_rs: rs::audio_output_callback_t = Some(cb_rs);
            assert_eq!(
                c::oracle_audio_output_connect(co, 0, ptr::null(), cb_c, ptr::null_mut()),
                rs::audio_output_connect(ro, 0, ptr::null(), cb_rs, ptr::null_mut())
            );
        }
        assert!(c::oracle_audio_output_connect(
            co,
            0,
            ptr::null(),
            Some(gr_d_c),
            cp
        ));
        assert!(rs::audio_output_connect(
            ro,
            0,
            ptr::null(),
            Some(gr_d_rs),
            rp
        ));

        let ra = ro as *mut AudioOutput;
        c::oracle_audio_tick(co, MAGIC + 21_000_000, MAGIC);
        let c_order = take_order();
        (*ra).tick_for_test(MAGIC + 21_000_000, MAGIC);
        let r_order = take_order();
        assert_eq!(c_order, r_order);
        assert_eq!(c_order, [4, 3, 2, 1]);
        assert_eq!(
            c::oracle_audio_mix_input_count(co, 0),
            (*ra).mix_input_count_for_test(0)
        );
        assert_eq!(c::oracle_audio_mix_input_count(co, 0), 5);

        c::oracle_audio_tick(co, MAGIC + 42_000_000, MAGIC);
        let c_order = take_order();
        (*ra).tick_for_test(MAGIC + 42_000_000, MAGIC);
        let r_order = take_order();
        assert_eq!(c_order, r_order);
        assert_eq!(c_order, [5, 4, 3, 2, 1]);
        close_pair(co, ro);
    }
}

// --- real-worker close/join lifetime --------------------------------------

/// Valid-context close/join fixture: a REAL worker (unsafe `open`, oracle
/// timing is the real clock/nanosleep) ticks on its own thread. Contexts
/// must survive stop+join at drop, and a disconnect'd output's context
/// must survive any in-flight callback return. Calls are counted in the
/// context itself so validity is observable after close.
struct RealCtx {
    // Written after `open` returns (the worker may already run): atomic so
    // the callback sees the published handle, not a torn/empty read.
    audio: AtomicPtr<c_void>,
    input_calls: AtomicUsize,
    output_calls: AtomicUsize,
    self_disc_calls: AtomicUsize,
}

unsafe extern "C" fn real_input(
    p: *mut c_void,
    _start: u64,
    end: u64,
    new_ts: *mut u64,
    _active: u32,
    mixes: *mut rs::audio_output_data,
) -> bool {
    let ctx = unsafe { &*(p as *const RealCtx) };
    ctx.input_calls.fetch_add(1, Ordering::SeqCst);
    unsafe {
        *new_ts = end;
        *(*mixes).data[0] = 0.5f32;
    }
    true
}

unsafe extern "C" fn real_output(p: *mut c_void, _m: usize, _d: *mut rs::audio_data) {
    let ctx = unsafe { &*(p as *const RealCtx) };
    ctx.output_calls.fetch_add(1, Ordering::SeqCst);
}

unsafe extern "C" fn real_self_disconnect(p: *mut c_void, m: usize, _d: *mut rs::audio_data) {
    let ctx = unsafe { &*(p as *const RealCtx) };
    ctx.self_disc_calls.fetch_add(1, Ordering::SeqCst);
    // In-flight callback disconnects itself, then must still return
    // through valid `ctx` — the param outlives the disconnect.
    // SAFETY: same-thread re-entrant call under the held recursive lock;
    // `audio` was published before `connect` took the lock.
    unsafe { &*(ctx.audio.load(Ordering::SeqCst) as *const AudioOutput) }.disconnect(
        m,
        Some(real_self_disconnect),
        p,
    );
}

#[test]
fn real_worker_close_join() {
    let _drive = DRIVER.lock().unwrap_or_else(|e| e.into_inner());
    let ctx = Box::new(RealCtx {
        audio: AtomicPtr::new(ptr::null_mut()),
        input_calls: AtomicUsize::new(0),
        output_calls: AtomicUsize::new(0),
        self_disc_calls: AtomicUsize::new(0),
    });
    let info = rs::audio_output_info {
        name: c"real".as_ptr(),
        samples_per_sec: 48000,
        format: FORMAT_FLOAT,
        speakers: SPEAKERS_STEREO,
        input_callback: Some(real_input),
        input_param: &*ctx as *const RealCtx as *mut c_void,
    };
    // SAFETY: `info.name` is a 'static string; `ctx` is boxed and read
    // only after drop returns (stop+join complete).
    let mut out = unsafe { AudioOutput::open(&info) }.expect("open");
    // Publish the handle before `connect` serializes it: the worker reads
    // `ctx.audio` inside the dispatch callback under the input lock.
    ctx.audio.store(
        &mut *out as *mut AudioOutput as *mut c_void,
        Ordering::SeqCst,
    );
    // SAFETY: `ctx` outlives both the disconnect below and `out`'s drop.
    unsafe {
        out.connect(
            0,
            None,
            Some(real_output),
            &*ctx as *const RealCtx as *mut c_void,
        );
        out.connect(
            1,
            None,
            Some(real_self_disconnect),
            &*ctx as *const RealCtx as *mut c_void,
        );
    }
    // Scheduler-independent waits: spin on the counters the callbacks bump —
    // a tick that never runs fails the test instead of guessing at timing.
    let deadline = std::time::Instant::now() + std::time::Duration::from_secs(30);
    while ctx.input_calls.load(Ordering::SeqCst) == 0
        || ctx.output_calls.load(Ordering::SeqCst) == 0
        || ctx.self_disc_calls.load(Ordering::SeqCst) == 0
    {
        std::hint::spin_loop();
        assert!(
            std::time::Instant::now() < deadline,
            "real worker never ticked: input={} output={} self_disc={}",
            ctx.input_calls.load(Ordering::SeqCst),
            ctx.output_calls.load(Ordering::SeqCst),
            ctx.self_disc_calls.load(Ordering::SeqCst)
        );
    }
    let calls_before = ctx.self_disc_calls.load(Ordering::SeqCst);
    // The real close path: stop+join through the raw handle (no `&mut`
    // while a callback can run), then drop. `Box::into_raw` hands the
    // FFI the owning pointer without forming an exclusive borrow.
    let raw = Box::into_raw(out) as *mut c_void;
    unsafe { rs::audio_output_close(raw) };
    // Close's join is complete: the worker is dead, so no callback can run
    // during or after it — counts are frozen. A short window gives any
    // (broken) stray callback a chance to trip the alarm; a correct close
    // passes regardless of timing.
    std::thread::sleep(std::time::Duration::from_millis(50));
    let calls_after = ctx.self_disc_calls.load(Ordering::SeqCst);
    assert!(calls_after >= 1 && calls_after == calls_before);
    // `ctx` itself is still valid here — input context survived stop/join,
    // output context survived disconnect + the in-flight callback return.
    let _ = ctx;
}

// --- concurrent connect/disconnect -----------------------------------------

unsafe extern "C" fn cd_a_c(_p: *mut c_void, _m: usize, d: *mut c::OracleAudioData) {
    if is_magic_ts(unsafe { *d }.timestamp) {
        push_order(1);
    }
}
unsafe extern "C" fn cd_a_rs(_p: *mut c_void, _m: usize, d: *mut rs::audio_data) {
    if is_magic_ts(unsafe { *d }.timestamp) {
        push_order(1);
    }
}
/// Dispatch counters: incremented inside a callback, i.e. while the
/// recursive input lock is held by the dispatch — the churn gate below.
static C_DISPATCHED: AtomicUsize = AtomicUsize::new(0);
static RS_DISPATCHED: AtomicUsize = AtomicUsize::new(0);
static C_CHURN_DONE: AtomicUsize = AtomicUsize::new(0);
static RS_CHURN_DONE: AtomicUsize = AtomicUsize::new(0);

unsafe extern "C" fn cd_b_c(_p: *mut c_void, _m: usize, d: *mut c::OracleAudioData) {
    C_DISPATCHED.fetch_add(1, Ordering::SeqCst);
    if is_magic_ts(unsafe { *d }.timestamp) {
        push_order(2);
    }
}
unsafe extern "C" fn cd_b_rs(_p: *mut c_void, _m: usize, d: *mut rs::audio_data) {
    RS_DISPATCHED.fetch_add(1, Ordering::SeqCst);
    if is_magic_ts(unsafe { *d }.timestamp) {
        push_order(2);
    }
}
unsafe extern "C" fn cd_hammer_c(_p: *mut c_void, _m: usize, _d: *mut c::OracleAudioData) {}
unsafe extern "C" fn cd_hammer_rs(_p: *mut c_void, _m: usize, _d: *mut rs::audio_data) {}

/// Spin until both counters reach `want`; a gate that never arrives fails
/// the test loudly instead of letting unsynchronized churn race on.
fn wait_churn(c: &AtomicUsize, r: &AtomicUsize, want: usize) {
    let deadline = std::time::Instant::now() + std::time::Duration::from_secs(30);
    while c.load(Ordering::SeqCst) < want || r.load(Ordering::SeqCst) < want {
        std::hint::spin_loop();
        assert!(
            std::time::Instant::now() < deadline,
            "churn gate timed out: c={} r={} want={want}",
            c.load(Ordering::SeqCst),
            r.load(Ordering::SeqCst)
        );
    }
}

/// Connect/disconnect contention bounded to the locked dispatch window:
/// churn threads only issue ops once a dispatch callback has run (the lock
/// is held then), so every op contends on the held recursive mutex and
/// completes between mix dispatches — never during the unlocked clamp,
/// which reads `inputs.num` in the C and would race a mutating list. The
/// driver joins each iteration's churn before the next tick (before the
/// next clamp).
#[test]
fn concurrent_connect_disconnect_parity() {
    let _drive = DRIVER.lock().unwrap_or_else(|e| e.into_inner());
    unsafe {
        let (co, ro) = open_pair();
        let hp_addr = 0xfeedusize;
        assert!(c::oracle_audio_output_connect(
            co,
            0,
            ptr::null(),
            Some(cd_a_c),
            ptr::null_mut()
        ));
        assert!(rs::audio_output_connect(
            ro,
            0,
            ptr::null(),
            Some(cd_a_rs),
            ptr::null_mut()
        ));
        assert!(c::oracle_audio_output_connect(
            co,
            0,
            ptr::null(),
            Some(cd_b_c),
            ptr::null_mut()
        ));
        assert!(rs::audio_output_connect(
            ro,
            0,
            ptr::null(),
            Some(cd_b_rs),
            ptr::null_mut()
        ));

        let co_addr = co as usize;
        let ro_addr = ro as usize;
        std::thread::scope(|s| {
            s.spawn(move || {
                let co = co_addr as *mut c::OracleAudioOutput;
                let hp = hp_addr as *mut c_void;
                for iter in 0..10usize {
                    // Only issue churn once this iteration's dispatch is
                    // underway (callback ran -> input lock held), so the
                    // ops queue on the mutex instead of racing a clamp.
                    let deadline = std::time::Instant::now() + std::time::Duration::from_secs(30);
                    while C_DISPATCHED.load(Ordering::SeqCst) <= iter {
                        std::hint::spin_loop();
                        assert!(
                            std::time::Instant::now() < deadline,
                            "c dispatch gate timed out at iter {iter}"
                        );
                    }
                    for _ in 0..10 {
                        c::oracle_audio_output_connect(co, 1, ptr::null(), Some(cd_hammer_c), hp);
                        c::oracle_audio_output_disconnect(co, 1, Some(cd_hammer_c), hp);
                    }
                    C_CHURN_DONE.fetch_add(1, Ordering::SeqCst);
                }
            });
            s.spawn(move || {
                let ro = ro_addr as *mut c_void;
                let hp = hp_addr as *mut c_void;
                for iter in 0..10usize {
                    let deadline = std::time::Instant::now() + std::time::Duration::from_secs(30);
                    while RS_DISPATCHED.load(Ordering::SeqCst) <= iter {
                        std::hint::spin_loop();
                        assert!(
                            std::time::Instant::now() < deadline,
                            "rs dispatch gate timed out at iter {iter}"
                        );
                    }
                    for _ in 0..10 {
                        rs::audio_output_connect(ro, 1, ptr::null(), Some(cd_hammer_rs), hp);
                        rs::audio_output_disconnect(ro, 1, Some(cd_hammer_rs), hp);
                    }
                    RS_CHURN_DONE.fetch_add(1, Ordering::SeqCst);
                }
            });
            let ra = ro as *mut AudioOutput;
            for i in 0..10u64 {
                c::oracle_audio_tick(co, MAGIC + 21_000_000 + i * 10_000, MAGIC);
                take_order();
                (*ra).tick_for_test(MAGIC + 21_000_000 + i * 10_000, MAGIC);
                take_order();
                // Join this tick's churn before the next tick — the next
                // clamp reads `inputs.num` without the mutex.
                wait_churn(&C_CHURN_DONE, &RS_CHURN_DONE, i as usize + 1);
            }
        });

        let ra = ro as *mut AudioOutput;
        c::oracle_audio_tick(co, MAGIC + 63_000_000, MAGIC);
        let c_order = take_order();
        (*ra).tick_for_test(MAGIC + 63_000_000, MAGIC);
        let r_order = take_order();
        assert_eq!(c_order, r_order);
        assert_eq!(c_order, [2, 1]);
        assert_eq!(
            c::oracle_audio_mix_input_count(co, 0),
            (*ra).mix_input_count_for_test(0)
        );
        assert_eq!(
            c::oracle_audio_mix_input_count(co, 1),
            (*ra).mix_input_count_for_test(1)
        );
        close_pair(co, ro);
    }
}
