//! C ABI for the frontend (`frontend/utility/obs-gpu-status.h`). The
//! frontend links it through the staticlib rust/obs-gpu-status-frontend,
//! like obs-nvenc links obs-nvenc-plugin, so libobs' exported symbol set
//! does not change.

use std::ffi::{CStr, CString, c_char, c_int};
use std::sync::Mutex;

use crate::report::Report;
use crate::{FrameWindow, LogCapture};

/// The graphics log lines [`obs_gpu_status_log_line`] kept.
static LOG: Mutex<LogCapture> = Mutex::new(LogCapture::new());

/// Offer one OBS log line; the ones about graphics are quoted in the
/// dialog (#944). Null is ignored.
///
/// # Safety
/// `line` is null or a NUL-terminated string.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn obs_gpu_status_log_line(line: *const c_char) {
    if line.is_null() {
        return;
    }
    // SAFETY: the caller passes a NUL-terminated string.
    let line = unsafe { CStr::from_ptr(line) }.to_string_lossy();
    // Every log line comes through here: filter before taking the lock.
    if crate::is_graphics_log_line(&line) {
        LOG.lock().unwrap_or_else(|e| e.into_inner()).push(&line);
    }
}

/// The frame-time window [`obs_gpu_status_sample`] fills.
static FRAMES: Mutex<FrameWindow> = Mutex::new(FrameWindow::new());

/// Record libobs' one-second average frame time
/// (`obs_get_average_frame_time_ns`); call once a second. 0 is skipped.
#[unsafe(no_mangle)]
pub extern "C" fn obs_gpu_status_sample(frame_ns: u64) {
    FRAMES
        .lock()
        .unwrap_or_else(|e| e.into_inner())
        .push(frame_ns);
}

/// The system's GPUs, probed once per process (`nvidia-smi` is too slow to
/// run on every refresh, or on the startup path: see
/// [`obs_gpu_status_prefetch`]).
static DEVICES: crate::DeviceCache = crate::DeviceCache::new();

/// Start probing the system's GPUs on a thread, so that the first
/// [`obs_gpu_status_report`] (after graphics init) finds them probed. Call
/// once, early; later calls do nothing.
#[unsafe(no_mangle)]
pub extern "C" fn obs_gpu_status_prefetch() {
    DEVICES.prefetch(crate::platform::system_devices);
}

/// Build the report for `adapter` (null when graphics init failed), the
/// system's GPUs, probed once per process (`nvidia-smi` is too slow to run
/// on every refresh), and the median of the sampled frame times
/// ([`FrameWindow::stats`]) against `interval_ns`. Writes
/// 0 (green), 1 (yellow) or 2 (red) to `state` when it is not null, and
/// returns the `gpuAcceleration` JSON, freed with [`obs_gpu_status_free`].
///
/// # Safety
/// `adapter` is null or a NUL-terminated string; `state` is null or
/// writable.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn obs_gpu_status_report(
    adapter: *const c_char,
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
    let frames = FRAMES
        .lock()
        .unwrap_or_else(|e| e.into_inner())
        .stats(interval_ns);
    let devices = DEVICES.get(crate::platform::system_devices);
    let log_lines = LOG.lock().unwrap_or_else(|e| e.into_inner()).lines();
    let report = Report::new(adapter.as_deref(), devices, frames).with_log_lines(log_lines);
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
