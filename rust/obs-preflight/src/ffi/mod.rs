//! Narrow C ABI for the headless preflight engine.
//!
//! All decision logic stays in the safe crate API. This module only owns the
//! opaque engine handle, copies fixed-layout samples, and copies completed
//! results into caller-owned storage.

use core::ffi::{CStr, c_char};
use core::{ptr, slice};

use crate::{
    BakeoffPlanner, CandidateMode, CandidatePlan, DEFAULT_WINDOW_DURATION_MS, StatSample, Verdict,
    WindowResult, expand_candidate_matrix, judge_window, roll_up_windows,
};

pub const STATUS_OK: i32 = 0;
pub const STATUS_INVALID_ARGUMENT: i32 = 1;
pub const STATUS_ANALYSIS_ERROR: i32 = 2;
pub const STATUS_OUT_OF_RANGE: i32 = 3;
pub const STATUS_COMPLETE: i32 = 4;
pub const REASON_CAPACITY: usize = 256;
pub const ENCODER_ID_CAPACITY: usize = 128;
const MAX_BAKEOFF_CANDIDATES: usize = 4096;

/// Mirrors `struct obs_preflight_sample` in the hand-written C header.
#[repr(C)]
#[derive(Clone, Copy, Debug)]
pub struct ObsPreflightSample {
    pub timestamp_ms: u64,
    pub average_frame_time_ns: u64,
    pub frame_interval_ns: u64,
    pub lagged_frames: u32,
    pub output_total_bytes: u64,
    pub output_total_frames: u64,
    pub output_frames_dropped: u64,
    pub output_congestion: f64,
    pub connect_time_ms: i32,
    pub encoder_total_frames: u64,
    pub encoder_skipped_frames: u64,
    pub cpu_percent: f64,
}

/// Mirrors `struct obs_preflight_window` in the hand-written C header.
#[repr(C)]
#[derive(Clone, Copy, Debug)]
pub struct ObsPreflightWindow {
    pub start_ms: u64,
    pub end_ms: u64,
    pub duration_ms: u64,
    pub output_bitrate_kbps: f64,
    pub output_bytes: u64,
    pub output_frames: u64,
    pub output_frames_dropped: u64,
    pub output_drop_percent: f64,
    pub encoder_frames: u64,
    pub encoder_frames_skipped: u64,
    pub encoder_skip_percent: f64,
    pub lagged_frames: u64,
    pub render_lag_percent: f64,
    pub average_frame_time_ns: f64,
    pub frame_interval_ns: f64,
    pub render_headroom_ns: i64,
    pub congestion_percent: f64,
    pub connect_time_ms: i32,
    pub cpu_percent: f64,
    pub verdict: i32,
    pub reason: [c_char; REASON_CAPACITY],
}

/// One video mode to test with every encoder ID passed to the planner.
#[repr(C)]
#[derive(Clone, Copy, Debug)]
pub struct ObsPreflightMode {
    pub width: u32,
    pub height: u32,
    pub fps_num: u32,
    pub fps_den: u32,
}

/// A planned candidate copied into caller-owned storage.
#[repr(C)]
#[derive(Clone, Copy, Debug)]
pub struct ObsPreflightCandidatePlan {
    pub order: usize,
    pub encoder: [c_char; ENCODER_ID_CAPACITY],
    pub width: u32,
    pub height: u32,
    pub fps_num: u32,
    pub fps_den: u32,
    pub duration_ms: u64,
    pub stop_if_fail: i32,
}

/// Opaque sequential planner for one UI bake-off.
pub struct PreflightBakeoff {
    planner: BakeoffPlanner,
}

/// Opaque, independently owned history for one preflight run.
pub struct PreflightEngine {
    window_duration_ms: u64,
    samples: Vec<StatSample>,
}

fn copy_candidate_plan(source: &CandidatePlan) -> Option<ObsPreflightCandidatePlan> {
    let encoder = source.candidate.encoder.as_bytes();
    if encoder.len() >= ENCODER_ID_CAPACITY {
        return None;
    }
    let mut result = ObsPreflightCandidatePlan {
        order: source.order,
        encoder: [0; ENCODER_ID_CAPACITY],
        width: source.candidate.width,
        height: source.candidate.height,
        fps_num: source.candidate.fps_num,
        fps_den: source.candidate.fps_den,
        duration_ms: source.duration_ms,
        stop_if_fail: i32::from(source.stop_if_fail),
    };
    for (destination, source) in result.encoder.iter_mut().zip(encoder) {
        *destination = *source as c_char;
    }
    Some(result)
}

impl From<ObsPreflightSample> for StatSample {
    fn from(sample: ObsPreflightSample) -> Self {
        Self {
            timestamp_ms: sample.timestamp_ms,
            average_frame_time_ns: sample.average_frame_time_ns,
            frame_interval_ns: sample.frame_interval_ns,
            lagged_frames: sample.lagged_frames,
            output_total_bytes: sample.output_total_bytes,
            output_total_frames: sample.output_total_frames,
            output_frames_dropped: sample.output_frames_dropped,
            output_congestion: sample.output_congestion,
            connect_time_ms: sample.connect_time_ms,
            encoder_total_frames: sample.encoder_total_frames,
            encoder_skipped_frames: sample.encoder_skipped_frames,
            cpu_percent: sample.cpu_percent,
        }
    }
}

fn results(engine: &PreflightEngine) -> Result<Vec<WindowResult>, ()> {
    roll_up_windows(&engine.samples, engine.window_duration_ms)
        .map(|windows| windows.into_iter().map(judge_window).collect())
        .map_err(|_| ())
}

fn copy_result(source: &WindowResult) -> ObsPreflightWindow {
    let rollup = &source.rollup;
    let mut result = ObsPreflightWindow {
        start_ms: rollup.start_ms,
        end_ms: rollup.end_ms,
        duration_ms: rollup.duration_ms,
        output_bitrate_kbps: rollup.output_bitrate_kbps,
        output_bytes: rollup.output_bytes,
        output_frames: rollup.output_frames,
        output_frames_dropped: rollup.output_frames_dropped,
        output_drop_percent: rollup.output_drop_percent,
        encoder_frames: rollup.encoder_frames,
        encoder_frames_skipped: rollup.encoder_frames_skipped,
        encoder_skip_percent: rollup.encoder_skip_percent,
        lagged_frames: rollup.lagged_frames,
        render_lag_percent: rollup.render_lag_percent,
        average_frame_time_ns: rollup.average_frame_time_ns,
        frame_interval_ns: rollup.frame_interval_ns,
        render_headroom_ns: rollup.render_headroom_ns,
        congestion_percent: rollup.congestion_percent,
        connect_time_ms: rollup.connect_time_ms,
        cpu_percent: rollup.cpu_percent,
        verdict: match source.verdict {
            Verdict::Pass => 0,
            Verdict::Marginal => 1,
            Verdict::Fail => 2,
        },
        reason: [0; REASON_CAPACITY],
    };
    // All verdict reasons are plain English. Keep one byte for the C NUL.
    for (destination, source) in result
        .reason
        .iter_mut()
        .zip(source.reason.as_bytes().iter().take(REASON_CAPACITY - 1))
    {
        *destination = *source as c_char;
    }
    result
}

/// Creates an engine using the standard five-second window.
#[unsafe(no_mangle)]
pub extern "C" fn obs_preflight_engine_create() -> *mut PreflightEngine {
    Box::into_raw(Box::new(PreflightEngine {
        window_duration_ms: DEFAULT_WINDOW_DURATION_MS,
        samples: Vec::new(),
    }))
}

/// Creates an engine with a caller-selected window duration.
///
/// Returns null when `window_duration_ms` is zero.
#[unsafe(no_mangle)]
pub extern "C" fn obs_preflight_engine_create_with_window(
    window_duration_ms: u64,
) -> *mut PreflightEngine {
    if window_duration_ms == 0 {
        return ptr::null_mut();
    }
    Box::into_raw(Box::new(PreflightEngine {
        window_duration_ms,
        samples: Vec::new(),
    }))
}

/// Destroys an engine previously returned by either create function. Null is accepted.
///
/// # Safety
///
/// A non-null `engine` must be a live handle returned by this module and must
/// not be used again after this call.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn obs_preflight_engine_destroy(engine: *mut PreflightEngine) {
    if !engine.is_null() {
        // SAFETY: guaranteed by the function contract.
        drop(unsafe { Box::from_raw(engine) });
    }
}

/// Copies a sample into the engine. Counters are cumulative within a run. Any
/// decrease starts a new result window and omits the interval crossing reset.
///
/// # Safety
///
/// `engine` must be a live handle returned by this module and `sample` must
/// point to a readable `ObsPreflightSample`.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn obs_preflight_engine_push_sample(
    engine: *mut PreflightEngine,
    sample: *const ObsPreflightSample,
) -> i32 {
    if engine.is_null() || sample.is_null() {
        return STATUS_INVALID_ARGUMENT;
    }
    // SAFETY: the caller provides a live engine and readable sample pointer.
    let sample = unsafe { *sample };
    // SAFETY: the caller provides exclusive mutable access to the engine.
    unsafe { &mut *engine }.samples.push(sample.into());
    STATUS_OK
}

/// Writes the number of completed windows to `count_out`.
///
/// Returns `STATUS_ANALYSIS_ERROR` until at least two valid samples exist.
///
/// # Safety
///
/// `engine` must be a live handle and `count_out` must point to writable
/// storage for one `size_t` value.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn obs_preflight_engine_get_window_count(
    engine: *const PreflightEngine,
    count_out: *mut usize,
) -> i32 {
    if engine.is_null() || count_out.is_null() {
        return STATUS_INVALID_ARGUMENT;
    }
    // SAFETY: the caller provides a live engine.
    let windows = match results(unsafe { &*engine }) {
        Ok(windows) => windows,
        Err(()) => return STATUS_ANALYSIS_ERROR,
    };
    // SAFETY: the caller provides writable output storage.
    unsafe { count_out.write(windows.len()) };
    STATUS_OK
}

/// Writes one rollup and verdict into caller-owned storage.
///
/// # Safety
///
/// `engine` must be a live handle and `window_out` must point to writable
/// storage for one `ObsPreflightWindow`.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn obs_preflight_engine_get_window(
    engine: *const PreflightEngine,
    index: usize,
    window_out: *mut ObsPreflightWindow,
) -> i32 {
    if engine.is_null() || window_out.is_null() {
        return STATUS_INVALID_ARGUMENT;
    }
    // SAFETY: the caller provides a live engine.
    let windows = match results(unsafe { &*engine }) {
        Ok(windows) => windows,
        Err(()) => return STATUS_ANALYSIS_ERROR,
    };
    let Some(window) = windows.get(index) else {
        return STATUS_OUT_OF_RANGE;
    };
    // SAFETY: the caller provides writable output storage.
    unsafe { window_out.write(copy_result(window)) };
    STATUS_OK
}

/// Creates an encoder-major Cartesian product of encoder IDs and video modes.
///
/// Returns null for empty, invalid, or overlong input and for a zero duration.
///
/// # Safety
///
/// When its length is non-zero, `encoder_ids` must point to `encoder_count`
/// readable C string pointers. `modes` must point to `mode_count` readable
/// `ObsPreflightMode` values. Every encoder string must be NUL terminated.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn obs_preflight_bakeoff_create(
    encoder_ids: *const *const c_char,
    encoder_count: usize,
    modes: *const ObsPreflightMode,
    mode_count: usize,
    duration_ms: u64,
) -> *mut PreflightBakeoff {
    if encoder_count == 0
        || mode_count == 0
        || encoder_count
            .checked_mul(mode_count)
            .is_none_or(|candidate_count| candidate_count > MAX_BAKEOFF_CANDIDATES)
        || encoder_ids.is_null()
        || modes.is_null()
        || duration_ms == 0
    {
        return ptr::null_mut();
    }

    // SAFETY: required by this function's pointer contract.
    let encoder_ptrs = unsafe { slice::from_raw_parts(encoder_ids, encoder_count) };
    let mut encoders = Vec::with_capacity(encoder_count);
    for encoder_ptr in encoder_ptrs {
        if encoder_ptr.is_null() {
            return ptr::null_mut();
        }
        // SAFETY: each element must be a valid NUL-terminated C string.
        let Ok(encoder) = unsafe { CStr::from_ptr(*encoder_ptr) }.to_str() else {
            return ptr::null_mut();
        };
        if encoder.len() >= ENCODER_ID_CAPACITY {
            return ptr::null_mut();
        }
        encoders.push(encoder.to_owned());
    }

    // SAFETY: required by this function's pointer contract.
    let modes = unsafe { slice::from_raw_parts(modes, mode_count) };
    let modes = modes
        .iter()
        .map(|mode| CandidateMode {
            width: mode.width,
            height: mode.height,
            fps_num: mode.fps_num,
            fps_den: mode.fps_den,
        })
        .collect::<Vec<_>>();
    let candidates = expand_candidate_matrix(&encoders, &modes);
    let Ok(planner) = BakeoffPlanner::new(candidates, duration_ms) else {
        return ptr::null_mut();
    };
    Box::into_raw(Box::new(PreflightBakeoff { planner }))
}

/// Destroys a planner returned by `obs_preflight_bakeoff_create`. Null is safe.
///
/// # Safety
///
/// A non-null planner must be a live handle returned by this module and must
/// not be used again after this call.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn obs_preflight_bakeoff_destroy(planner: *mut PreflightBakeoff) {
    if !planner.is_null() {
        // SAFETY: guaranteed by the function contract.
        drop(unsafe { Box::from_raw(planner) });
    }
}

/// Writes the next candidate. Returns `STATUS_COMPLETE` after the last result.
///
/// # Safety
///
/// `planner` must be a live handle. `candidate_out` must point to writable
/// storage for one `ObsPreflightCandidatePlan`.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn obs_preflight_bakeoff_next(
    planner: *mut PreflightBakeoff,
    candidate_out: *mut ObsPreflightCandidatePlan,
) -> i32 {
    if planner.is_null() || candidate_out.is_null() {
        return STATUS_INVALID_ARGUMENT;
    }
    // SAFETY: the caller provides an exclusively borrowed live planner.
    let planner = unsafe { &mut *planner };
    let plan = match planner.planner.next_candidate() {
        Ok(Some(plan)) => plan,
        Ok(None) => return STATUS_COMPLETE,
        Err(_) => return STATUS_ANALYSIS_ERROR,
    };
    let Some(plan) = copy_candidate_plan(&plan) else {
        return STATUS_OUT_OF_RANGE;
    };
    // SAFETY: the caller provides writable output storage.
    unsafe { candidate_out.write(plan) };
    STATUS_OK
}

/// Records a completed candidate. A failed candidate is preserved and the
/// next call to `obs_preflight_bakeoff_next` still advances to the next row.
///
/// # Safety
///
/// `planner` must be a live handle with one candidate in progress.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn obs_preflight_bakeoff_complete(
    planner: *mut PreflightBakeoff,
    verdict: i32,
) -> i32 {
    if planner.is_null() {
        return STATUS_INVALID_ARGUMENT;
    }
    let verdict = match verdict {
        0 => Verdict::Pass,
        1 => Verdict::Marginal,
        2 => Verdict::Fail,
        _ => return STATUS_INVALID_ARGUMENT,
    };
    // SAFETY: the caller provides an exclusively borrowed live planner.
    match unsafe { &mut *planner }.planner.complete_candidate(verdict) {
        Ok(()) => STATUS_OK,
        Err(_) => STATUS_ANALYSIS_ERROR,
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::ffi::{CStr, CString};
    use std::mem::{align_of, offset_of, size_of};

    #[test]
    fn c_struct_layout_matches_the_hand_written_header() {
        // These offsets are checked against the public C header by the cmocka
        // collector test's compile-time sizeof/offsetof assertions.
        assert_eq!(offset_of!(ObsPreflightSample, timestamp_ms), 0);
        assert_eq!(offset_of!(ObsPreflightSample, average_frame_time_ns), 8);
        assert_eq!(offset_of!(ObsPreflightSample, frame_interval_ns), 16);
        assert_eq!(offset_of!(ObsPreflightSample, lagged_frames), 24);
        assert_eq!(offset_of!(ObsPreflightSample, output_total_bytes), 32);
        assert_eq!(offset_of!(ObsPreflightSample, output_total_frames), 40);
        assert_eq!(offset_of!(ObsPreflightSample, output_frames_dropped), 48);
        assert_eq!(offset_of!(ObsPreflightSample, output_congestion), 56);
        assert_eq!(offset_of!(ObsPreflightSample, connect_time_ms), 64);
        assert_eq!(offset_of!(ObsPreflightSample, encoder_total_frames), 72);
        assert_eq!(offset_of!(ObsPreflightSample, encoder_skipped_frames), 80);
        assert_eq!(offset_of!(ObsPreflightSample, cpu_percent), 88);
        assert_eq!(size_of::<ObsPreflightSample>(), 96);
        assert_eq!(align_of::<ObsPreflightSample>(), 8);

        assert_eq!(offset_of!(ObsPreflightWindow, verdict), 152);
        assert_eq!(offset_of!(ObsPreflightWindow, reason), 156);
        assert_eq!(size_of::<ObsPreflightWindow>(), 416);
        assert_eq!(align_of::<ObsPreflightWindow>(), 8);

        assert_eq!(size_of::<ObsPreflightMode>(), 16);
        assert_eq!(offset_of!(ObsPreflightCandidatePlan, encoder), 8);
        assert_eq!(offset_of!(ObsPreflightCandidatePlan, width), 136);
        assert_eq!(offset_of!(ObsPreflightCandidatePlan, duration_ms), 152);
        assert_eq!(offset_of!(ObsPreflightCandidatePlan, stop_if_fail), 160);
        assert_eq!(size_of::<ObsPreflightCandidatePlan>(), 168);
        assert_eq!(align_of::<ObsPreflightCandidatePlan>(), 8);
    }

    #[test]
    fn c_bakeoff_abi_preserves_matrix_order_and_advances_after_failure() {
        let encoders = [
            CString::new("obs_x264").unwrap(),
            CString::new("obs_nvenc_h264").unwrap(),
        ];
        let encoder_ptrs = [encoders[0].as_ptr(), encoders[1].as_ptr()];
        let modes = [
            ObsPreflightMode {
                width: 1920,
                height: 1080,
                fps_num: 60,
                fps_den: 1,
            },
            ObsPreflightMode {
                width: 1280,
                height: 720,
                fps_num: 30,
                fps_den: 1,
            },
        ];
        // SAFETY: the string pointers and arrays stay alive for the constructor call.
        let planner = unsafe {
            obs_preflight_bakeoff_create(
                encoder_ptrs.as_ptr(),
                encoder_ptrs.len(),
                modes.as_ptr(),
                modes.len(),
                5000,
            )
        };
        assert!(!planner.is_null());

        let mut planned = std::mem::MaybeUninit::<ObsPreflightCandidatePlan>::uninit();
        // SAFETY: `planner` is live and `planned` points to writable storage.
        assert_eq!(
            unsafe { obs_preflight_bakeoff_next(planner, planned.as_mut_ptr()) },
            STATUS_OK
        );
        // SAFETY: the previous call initialized the output on STATUS_OK.
        let first = unsafe { planned.assume_init() };
        assert_eq!(first.order, 1);
        assert_eq!(
            unsafe { CStr::from_ptr(first.encoder.as_ptr()) }.to_bytes(),
            b"obs_x264"
        );
        assert_eq!(
            (first.width, first.height, first.fps_num, first.fps_den),
            (1920, 1080, 60, 1)
        );

        // A failed candidate is recorded; it must not terminate the planner.
        // SAFETY: `planner` is live and has one candidate in progress.
        assert_eq!(
            unsafe { obs_preflight_bakeoff_complete(planner, 2) },
            STATUS_OK
        );
        // SAFETY: `planner` is live and `planned` is writable.
        assert_eq!(
            unsafe { obs_preflight_bakeoff_next(planner, planned.as_mut_ptr()) },
            STATUS_OK
        );
        // SAFETY: the previous call initialized the output on STATUS_OK.
        let second = unsafe { planned.assume_init() };
        assert_eq!(second.order, 2);
        assert_eq!(
            (second.width, second.height, second.fps_num, second.fps_den),
            (1280, 720, 30, 1)
        );

        // SAFETY: `planner` is live; destroy consumes it.
        unsafe { obs_preflight_bakeoff_destroy(planner) };
    }
}
