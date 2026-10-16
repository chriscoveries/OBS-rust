//! C ABI for the frontend (`frontend/utility/obs-gpu-status.h`). The
//! frontend links it through the staticlib rust/obs-gpu-status-frontend,
//! like obs-nvenc links obs-nvenc-plugin, so libobs' exported symbol set
//! does not change.

use std::ffi::{CStr, CString, c_char, c_int};
use std::sync::OnceLock;

use crate::FrameStats;
use crate::report::Report;

/// Build the report for `adapter` (null when graphics init failed) and the
/// system's GPUs, probed once per process (`nvidia-smi` is too slow to run
/// on every refresh). `frame_ns` holds `count` one-second frame-time
/// samples, judged by their median ([`FrameStats::from_samples`]). Writes
/// 0 (green), 1 (yellow) or 2 (red) to `state` when it is not null, and
/// returns the `gpuAcceleration` JSON, freed with [`obs_gpu_status_free`].
///
/// # Safety
/// `adapter` is null or a NUL-terminated string; `frame_ns` is null or
/// points to `count` readable values; `state` is null or writable.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn obs_gpu_status_report(
    adapter: *const c_char,
    frame_ns: *const u64,
    count: usize,
    interval_ns: u64,
    state: *mut c_int,
) -> *mut c_char {
    let adapter = if adapter.is_null() {
        None
    } else {
        // SAFETY: the caller passes a NUL-terminated string.
        Some(
            unsafe { CStr::from_ptr(adapter) }
                .to_string_lossy()
                .into_owned(),
        )
    };
    let samples: &[u64] = if frame_ns.is_null() || count == 0 {
        &[]
    } else {
        // SAFETY: the caller passes `count` readable values.
        unsafe { std::slice::from_raw_parts(frame_ns, count) }
    };
    let frames = FrameStats::from_samples(samples, interval_ns);
    static DEVICES: OnceLock<Vec<crate::GpuDevice>> = OnceLock::new();
    let devices = DEVICES.get_or_init(crate::platform::system_devices).clone();
    let report = Report::new(adapter.as_deref(), devices, frames);
    if !state.is_null() {
        // SAFETY: the caller passes a writable int.
        unsafe { *state = report.state as c_int };
    }
    // The JSON escapes control characters, so it has no interior NUL.
    CString::new(report.to_json()).map_or(std::ptr::null_mut(), CString::into_raw)
}

/// Free a string from [`obs_gpu_status_report`]; null is ignored.
///
/// # Safety
/// `json` is null or came from [`obs_gpu_status_report`] and is not used
/// again.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn obs_gpu_status_free(json: *mut c_char) {
    if !json.is_null() {
        // SAFETY: allocated by CString::into_raw in obs_gpu_status_report.
        drop(unsafe { CString::from_raw(json) });
    }
}
