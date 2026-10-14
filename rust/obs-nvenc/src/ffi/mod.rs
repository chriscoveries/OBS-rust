//! C ABI shim for the CUDA driver loader (obs-rust/obs-studio#524).
//!
//! `plugins/obs-nvenc/cuda-load-rust.c` replaces `init_cuda_internal`'s
//! `os_dlopen`/`os_dlsym` walk with [`obs_nvenc_cuda_fill`]. C keeps the
//! `CudaFunctions` layout: it passes the `{offsetof, name}` table it already
//! had, so no CUDA struct is mirrored here and the table cannot drift from
//! the header.

use std::ffi::{CStr, c_char, c_int, c_void};
use std::slice;

use crate::{CudaDriver, LoadError};

/// The table was filled.
pub const OBS_NVENC_CUDA_OK: c_int = 0;
/// The driver library could not be loaded; C reports
/// `Loading CUDA library failed.`
pub const OBS_NVENC_CUDA_LIB_FAILED: c_int = 1;
/// A function is missing; C reports `Loading CUDA functions failed.`
pub const OBS_NVENC_CUDA_FUNCS_FAILED: c_int = 2;

/// One `cuda_functions[]` row: where in `CudaFunctions` the pointer goes and
/// the exported symbol it comes from.
#[repr(C)]
#[derive(Debug, Clone, Copy)]
pub struct ObsNvencCudaEntry {
    pub offset: isize,
    pub name: *const c_char,
}

/// Fills `table` (a `CudaFunctions`) from `loaded`. On failure returns the
/// `OBS_NVENC_CUDA_*` code and writes the log text (NUL-terminated,
/// truncated) to `err`.
///
/// # Safety
///
/// Each `entries[i].name` is a NUL-terminated string and
/// `table + entries[i].offset` is a writable pointer-sized slot.
pub unsafe fn fill_table(
    loaded: Result<&CudaDriver, &LoadError>,
    table: *mut u8,
    entries: &[ObsNvencCudaEntry],
    err: &mut [u8],
) -> c_int {
    let driver = match loaded {
        Ok(driver) => driver,
        // C: os_dlsym failed -> "Loading CUDA functions failed."; the
        // load_cuda_func log line is `[obs-nvenc] Could not load function`.
        Err(LoadError::MissingSymbol { symbol, .. }) => {
            write_message(
                err,
                &format!("[obs-nvenc] Could not load function: {symbol}"),
            );
            return OBS_NVENC_CUDA_FUNCS_FAILED;
        }
        // C: os_dlopen failed -> "Loading CUDA library failed."; the text
        // is `[NVENC] Test process failed: cuda_lib` plus what was tried.
        Err(e) => {
            write_message(err, &e.to_string());
            return OBS_NVENC_CUDA_LIB_FAILED;
        }
    };
    // Resolve everything before writing anything, so a failure leaves the
    // table untouched.
    let mut resolved = Vec::with_capacity(entries.len());
    for entry in entries {
        // SAFETY: the caller guarantees a NUL-terminated name.
        let name = unsafe { CStr::from_ptr(entry.name) }.to_string_lossy();
        let Some(sym) = driver.symbol(&name) else {
            write_message(err, &format!("[obs-nvenc] Could not load function: {name}"));
            return OBS_NVENC_CUDA_FUNCS_FAILED;
        };
        resolved.push((entry.offset, sym));
    }
    for (offset, sym) in resolved {
        // SAFETY: the caller guarantees `table + offset` is a writable slot.
        unsafe {
            table
                .offset(offset)
                .cast::<*mut c_void>()
                .write_unaligned(sym.as_ptr());
        }
    }
    OBS_NVENC_CUDA_OK
}

/// Copies `msg` into `err`, truncated, always NUL-terminated.
fn write_message(err: &mut [u8], msg: &str) {
    let Some(room) = err.len().checked_sub(1) else {
        return;
    };
    let n = msg.len().min(room);
    err[..n].copy_from_slice(&msg.as_bytes()[..n]);
    err[n] = 0;
}

/// Loads the CUDA driver and fills `table`; see [`fill_table`]. The driver
/// stays loaded for the life of the process, as the C code never `dlclose`d
/// it.
///
/// # Safety
///
/// `entries` points to `count` rows as in [`fill_table`]; `err` is writable
/// for `err_len` bytes (or null with `err_len` 0).
#[unsafe(no_mangle)]
pub unsafe extern "C" fn obs_nvenc_cuda_fill(
    table: *mut c_void,
    entries: *const ObsNvencCudaEntry,
    count: usize,
    err: *mut c_char,
    err_len: usize,
) -> c_int {
    let err: &mut [u8] = if err.is_null() {
        &mut []
    } else {
        // SAFETY: writable for `err_len` bytes per the contract.
        unsafe { slice::from_raw_parts_mut(err.cast::<u8>(), err_len) }
    };
    let entries: &[ObsNvencCudaEntry] = if entries.is_null() {
        &[]
    } else {
        // SAFETY: `count` rows per the contract.
        unsafe { slice::from_raw_parts(entries, count) }
    };
    let loaded = CudaDriver::load();
    // SAFETY: forwarded contract.
    let rc = unsafe { fill_table(loaded.as_ref(), table.cast(), entries, err) };
    // Never dlclose: the filled pointers must stay valid.
    std::mem::forget(loaded);
    rc
}
