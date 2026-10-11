//! Tier 3: the Rust audio-io C ABI behaves exactly like the original C,
//! compiled as an oracle. State is compared through identical call
//! sequences; ticks are driven explicitly with a magic `start_ts` so the
//! real audio threads' asynchronous input-callback invocations are ignored.
//!
//! `audio_resampler_create` is stubbed to NULL inside the oracle, so both
//! sides refuse a `conversion` that differs from the output's own format —
//! the parity check covers the refusal path, not resampling itself.
//!
//! Unix-first: the port is `#[cfg(unix)]`; Windows keeps media-io/audio-io.c.

#![cfg(unix)]

use core::ffi::{c_char, c_int, c_void};
use core::ptr;
use std::sync::Mutex;

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
/// The real worker threads both sides spawn carry `os_gettime_ns` values
/// (~1e18 ns) and must never inject records: every recorded action below
/// checks this gate, not just the input callbacks.
fn is_magic_ts(ts: u64) -> bool {
    (MAGIC..MAGIC.wrapping_add(1_000_000_000)).contains(&ts)
}

/// Serializes the whole test driver: `open` spawns real audio threads on
/// both sides, so every test holds this for its entire body — otherwise
/// parallel `#[test]`s would interleave into the shared TICK/ORDER state.
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

/// Open both sides; return the oracle handle and the Rust handle (which is
/// the `*mut c_void` `audio_t`, i.e. `*mut AudioOutput`).
unsafe fn open_pair() -> (*mut c::OracleAudioOutput, *mut c_void) {
    // Every open needs a real input callback: the spawned audio thread
    // invokes it unconditionally, so a NULL cb faults on both sides.
    let mut co: *mut c::OracleAudioOutput = ptr::null_mut();
    let mut info = info_c(None);
    info.input_callback = Some(input_c);
    assert_eq!(
        unsafe { c::oracle_audio_output_open(&mut co, &mut info) },
        0,
        "oracle open"
    );
    let mut ro: *mut c_void = ptr::null_mut();
    assert_eq!(
        unsafe { rs::audio_output_open(&mut ro, &mut info_rs(Some(input_rs))) },
        rs::AUDIO_OUTPUT_SUCCESS,
        "rust open"
    );
    (co, ro)
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
        c::oracle_audio_output_close(co);
        rs::audio_output_close(ro);
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
        c::oracle_audio_output_close(co);
        rs::audio_output_close(ro);
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
        assert_eq!(c::oracle_audio_output_open(&mut co, &mut info), 0);
        let mut ro: *mut c_void = ptr::null_mut();
        let mut ri = info_rs(Some(input_rs));
        assert_eq!(
            rs::audio_output_open(&mut ro, &mut ri),
            rs::AUDIO_OUTPUT_SUCCESS
        );
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

        c::oracle_audio_output_close(co);
        rs::audio_output_close(ro);
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
        acc = acc.wrapping_mul(31).wrapping_add(unsafe { *d.data[0].cast::<f32>().add(i) }.to_bits());
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
        acc = acc.wrapping_mul(31).wrapping_add(unsafe { *d.data[0].cast::<f32>().add(i) }.to_bits());
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
            assert_eq!(c::oracle_audio_output_open(&mut co, &mut ci), 0);
            let mut ro: *mut c_void = ptr::null_mut();
            let mut ri = info_rs(Some(input_marked_rs));
            ri.input_param = marker;
            assert_eq!(rs::audio_output_open(&mut ro, &mut ri), rs::AUDIO_OUTPUT_SUCCESS);

            for i in 0..n_inputs {
                if clip_flags[i] {
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

            c::oracle_audio_output_close(co);
            rs::audio_output_close(ro);
        }
    }
}

/* ------------------------------------------------------------------ */
/* Reentrancy regressions (fork PR #35 review acceptance): the same     */
/* callback program runs on the oracle and on the port, and the        */
/* recorded order must be identical. Each ctx carries the handle the    */
/* re-entrant callback needs; everything is gated on is_magic_ts so    */
/* the real worker ticks can never fire these paths.                    */

/// Handle the re-entrant callbacks operate on (oracle or rust audio_t).
struct ReCtx {
    audio: *mut c_void,
    extra: *mut c_void,
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
        assert!(c::oracle_audio_output_connect(co, 0, ptr::null(), Some(sd_other_c), ptr::null_mut()));
        assert!(rs::audio_output_connect(ro, 0, ptr::null(), Some(sd_other_rs), ptr::null_mut()));
        assert!(c::oracle_audio_output_connect(co, 0, ptr::null(), Some(sd_self_c), cp));
        assert!(rs::audio_output_connect(ro, 0, ptr::null(), Some(sd_self_rs), rp));

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
        c::oracle_audio_output_close(co);
        rs::audio_output_close(ro);
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
        assert!(c::oracle_audio_output_connect(co, 0, ptr::null(), Some(rm_a_c), a_param));
        assert!(rs::audio_output_connect(ro, 0, ptr::null(), Some(rm_a_rs), a_param));
        assert!(c::oracle_audio_output_connect(co, 0, ptr::null(), Some(rm_b_c), cp));
        assert!(rs::audio_output_connect(ro, 0, ptr::null(), Some(rm_b_rs), rp));
        assert!(c::oracle_audio_output_connect(co, 0, ptr::null(), Some(rm_c_c), ptr::null_mut()));
        assert!(rs::audio_output_connect(ro, 0, ptr::null(), Some(rm_c_rs), ptr::null_mut()));

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
        c::oracle_audio_output_close(co);
        rs::audio_output_close(ro);
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
    unsafe { c::oracle_audio_output_connect(ctx.audio as _, 0, ptr::null(), Some(gr_e_c), ctx.extra) };
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
        assert!(c::oracle_audio_output_connect(co, 0, ptr::null(), Some(gr_d_c), cp));
        assert!(rs::audio_output_connect(ro, 0, ptr::null(), Some(gr_d_rs), rp));

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
        c::oracle_audio_output_close(co);
        rs::audio_output_close(ro);
    }
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
unsafe extern "C" fn cd_b_c(_p: *mut c_void, _m: usize, d: *mut c::OracleAudioData) {
    if is_magic_ts(unsafe { *d }.timestamp) {
        push_order(2);
    }
}
unsafe extern "C" fn cd_b_rs(_p: *mut c_void, _m: usize, d: *mut rs::audio_data) {
    if is_magic_ts(unsafe { *d }.timestamp) {
        push_order(2);
    }
}
unsafe extern "C" fn cd_hammer_c(_p: *mut c_void, _m: usize, _d: *mut c::OracleAudioData) {}
unsafe extern "C" fn cd_hammer_rs(_p: *mut c_void, _m: usize, _d: *mut rs::audio_data) {}

/// Connect/disconnect racing a dispatch from another thread: a hammer
/// thread churns the list while the driver ticks; deterministic check —
/// both sides survive and a quiesced final tick reproduces the same
/// callback order.
#[test]
fn concurrent_connect_disconnect_parity() {
    let _drive = DRIVER.lock().unwrap_or_else(|e| e.into_inner());
    unsafe {
        let (co, ro) = open_pair();
        let hp_addr = 0xfeedusize;
        assert!(c::oracle_audio_output_connect(co, 0, ptr::null(), Some(cd_a_c), ptr::null_mut()));
        assert!(rs::audio_output_connect(ro, 0, ptr::null(), Some(cd_a_rs), ptr::null_mut()));
        assert!(c::oracle_audio_output_connect(co, 0, ptr::null(), Some(cd_b_c), ptr::null_mut()));
        assert!(rs::audio_output_connect(ro, 0, ptr::null(), Some(cd_b_rs), ptr::null_mut()));

        let co_addr = co as usize;
        let ro_addr = ro as usize;
        std::thread::scope(|s| {
            s.spawn(move || {
                let co = co_addr as *mut c::OracleAudioOutput;
                let hp = hp_addr as *mut c_void;
                for _ in 0..100 {
                    unsafe {
                        c::oracle_audio_output_connect(co, 1, ptr::null(), Some(cd_hammer_c), hp);
                        c::oracle_audio_output_disconnect(co, 1, Some(cd_hammer_c), hp);
                    }
                }
            });
            s.spawn(move || {
                let ro = ro_addr as *mut c_void;
                let hp = hp_addr as *mut c_void;
                for _ in 0..100 {
                    unsafe {
                        rs::audio_output_connect(ro, 1, ptr::null(), Some(cd_hammer_rs), hp);
                        rs::audio_output_disconnect(ro, 1, Some(cd_hammer_rs), hp);
                    }
                }
            });
            let ra = ro as *mut AudioOutput;
            for i in 0..10u64 {
                c::oracle_audio_tick(co, MAGIC + 21_000_000 + i * 10_000, MAGIC);
                take_order();
                (*ra).tick_for_test(MAGIC + 21_000_000 + i * 10_000, MAGIC);
                take_order();
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
        c::oracle_audio_output_close(co);
        rs::audio_output_close(ro);
    }
}
