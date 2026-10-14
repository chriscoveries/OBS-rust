//! C ABI shim for `libobs/util/platform-cocoa.m`.
//!
//! The safe core is `platform::unix::cocoa`. This file converts pointers
//! and delegates. `#[cfg(unix)]` on the module is what the export scanner
//! uses to mark every symbol here `unix`.

use core::ffi::{CStr, c_char, c_int, c_void};
use core::ptr;
use core::slice;

use super::dstr::{self as dstr_ffi, dstr};
use super::platform_nix::os_proc_memory_usage;
use crate::platform::unix::cocoa;

fn cstr<'a>(p: *const c_char) -> Option<&'a [u8]> {
    if p.is_null() {
        return None;
    }
    // SAFETY: `p` is a C string when non-null.
    Some(unsafe { CStr::from_ptr(p) }.to_bytes())
}

/// `bmalloc` of `bytes` plus a NUL.
unsafe fn alloc_bytes(bytes: &[u8]) -> *mut c_char {
    // SAFETY: the size is `len + 1`, never zero for the callers here.
    let p = unsafe { crate::ffi::bmem::bmalloc(bytes.len() + 1) }.cast::<u8>();
    // SAFETY: `bmalloc` returned `len + 1` bytes.
    unsafe {
        ptr::copy_nonoverlapping(bytes.as_ptr(), p, bytes.len());
        *p.add(bytes.len()) = 0;
    }
    p.cast()
}

/// `bmalloc` of a buffer that already includes its trailing NUL.
unsafe fn alloc_terminated(bytes: &[u8]) -> *mut c_char {
    // SAFETY: `bytes` is non-empty (it holds at least the NUL).
    let p = unsafe { crate::ffi::bmem::bmalloc(bytes.len()) }.cast::<u8>();
    // SAFETY: `bmalloc` returned `bytes.len()` bytes.
    unsafe { ptr::copy_nonoverlapping(bytes.as_ptr(), p, bytes.len()) };
    p.cast()
}

#[cfg(unix)]
mod shims {
    use super::*;

    /// # Safety
    /// No pointer arguments.
    #[unsafe(no_mangle)]
    pub unsafe extern "C" fn os_gettime_ns() -> u64 {
        cocoa::gettime_ns()
    }

    /// # Safety
    /// `dst` is `size` bytes or null. `name` is a C string or null.
    /// An empty Application Support directory calls `bcrash`, as in C.
    #[unsafe(no_mangle)]
    pub unsafe extern "C" fn os_get_config_path(
        dst: *mut c_char,
        size: usize,
        name: *const c_char,
    ) -> c_int {
        let name = cstr(name);
        if dst.is_null() || size == 0 {
            return cocoa::config_path_into(None, name);
        }
        // SAFETY: the caller guarantees `size` bytes at `dst`.
        let buf = unsafe { slice::from_raw_parts_mut(dst.cast::<u8>(), size) };
        cocoa::config_path_into(Some(buf), name)
    }

    /// # Safety
    /// `name` is a C string or null. The result is `bmalloc`'d.
    #[unsafe(no_mangle)]
    pub unsafe extern "C" fn os_get_config_path_ptr(name: *const c_char) -> *mut c_char {
        let bytes = cocoa::config_path_ptr(cstr(name));
        // SAFETY: the path is non-empty (the ptr form always ends in `/`).
        unsafe { alloc_bytes(&bytes) }
    }

    /// # Safety
    /// `dst` is `size` bytes or null. `name` is a C string or null.
    #[unsafe(no_mangle)]
    pub unsafe extern "C" fn os_get_program_data_path(
        dst: *mut c_char,
        size: usize,
        name: *const c_char,
    ) -> c_int {
        let name = cstr(name);
        if dst.is_null() || size == 0 {
            return cocoa::program_data_path_into(None, name);
        }
        // SAFETY: the caller guarantees `size` bytes at `dst`.
        let buf = unsafe { slice::from_raw_parts_mut(dst.cast::<u8>(), size) };
        cocoa::program_data_path_into(Some(buf), name)
    }

    /// # Safety
    /// `name` is a C string or null. The result is `bmalloc`'d.
    #[unsafe(no_mangle)]
    pub unsafe extern "C" fn os_get_program_data_path_ptr(name: *const c_char) -> *mut c_char {
        let bytes = cocoa::program_data_path_ptr(cstr(name));
        // SAFETY: the path is non-empty.
        unsafe { alloc_bytes(&bytes) }
    }

    /// # Safety
    /// `name` is a C string or null. The result is `bmalloc`'d, or null
    /// when the executable path cannot be read.
    #[unsafe(no_mangle)]
    pub unsafe extern "C" fn os_get_executable_path_ptr(name: *const c_char) -> *mut c_char {
        let Some(bytes) = cocoa::executable_path(cstr(name)) else {
            return ptr::null_mut();
        };
        // SAFETY: a resolved path contains at least the trailing slash.
        unsafe { alloc_bytes(&bytes) }
    }

    /// # Safety
    /// The result is opaque. Destroy it with `os_cpu_usage_info_destroy`.
    #[unsafe(no_mangle)]
    pub unsafe extern "C" fn os_cpu_usage_info_start() -> *mut c_void {
        match cocoa::cpu_usage_start() {
            Some(info) => Box::into_raw(Box::new(info)).cast(),
            None => ptr::null_mut(),
        }
    }

    /// # Safety
    /// `info` came from `os_cpu_usage_info_start` or is null.
    #[unsafe(no_mangle)]
    pub unsafe extern "C" fn os_cpu_usage_info_query(info: *mut c_void) -> f64 {
        if info.is_null() {
            return 0.0;
        }
        // SAFETY: `info` is a live `CpuUsage`.
        cocoa::cpu_usage_query(Some(unsafe { &mut *info.cast::<cocoa::CpuUsage>() }))
    }

    /// # Safety
    /// `info` came from `os_cpu_usage_info_start` or is null.
    #[unsafe(no_mangle)]
    pub unsafe extern "C" fn os_cpu_usage_info_destroy(info: *mut c_void) {
        if info.is_null() {
            return;
        }
        // SAFETY: `info` is a live `CpuUsage` from `Box::into_raw`.
        drop(unsafe { Box::from_raw(info.cast::<cocoa::CpuUsage>()) });
    }

    /// # Safety
    /// `reason` is a C string or null. The result is a retained activity
    /// token. End it with `os_end_high_performance`.
    #[unsafe(no_mangle)]
    pub unsafe extern "C" fn os_request_high_performance(reason: *const c_char) -> *mut c_void {
        match cocoa::request_high_performance(cstr(reason)) {
            Some(token) => token.into_raw(),
            None => ptr::null_mut(),
        }
    }

    /// # Safety
    /// `token` came from `os_request_high_performance` or is null.
    #[unsafe(no_mangle)]
    pub unsafe extern "C" fn os_end_high_performance(token: *mut c_void) {
        // SAFETY: `token` is the retained activity from
        // `os_request_high_performance`, or null. This call owns it.
        cocoa::end_high_performance(unsafe { cocoa::PerformanceToken::from_raw(token) });
    }

    /// # Safety
    /// `reason` is a C string or null. Destroy the result with
    /// `os_inhibit_sleep_destroy`.
    #[unsafe(no_mangle)]
    pub unsafe extern "C" fn os_inhibit_sleep_create(reason: *const c_char) -> *mut c_void {
        Box::into_raw(Box::new(cocoa::inhibit_create(cstr(reason)))).cast()
    }

    /// # Safety
    /// `info` came from `os_inhibit_sleep_create` or is null.
    #[unsafe(no_mangle)]
    pub unsafe extern "C" fn os_inhibit_sleep_set_active(info: *mut c_void, active: bool) -> bool {
        if info.is_null() {
            return false;
        }
        // SAFETY: `info` is a live `Inhibit`.
        cocoa::inhibit_set_active(Some(unsafe { &mut *info.cast::<cocoa::Inhibit>() }), active)
    }

    /// # Safety
    /// `info` came from `os_inhibit_sleep_create` or is null.
    #[unsafe(no_mangle)]
    pub unsafe extern "C" fn os_inhibit_sleep_destroy(info: *mut c_void) {
        if info.is_null() {
            return;
        }
        // SAFETY: `info` is a live `Inhibit` from `Box::into_raw`.
        let info = unsafe { Box::from_raw(info.cast::<cocoa::Inhibit>()) };
        cocoa::inhibit_destroy(Some(*info));
    }

    /// # Safety
    /// No pointer arguments.
    #[unsafe(no_mangle)]
    pub unsafe extern "C" fn os_get_emulation_status() -> bool {
        cocoa::emulation_status()
    }

    /// # Safety
    /// No pointer arguments.
    #[unsafe(no_mangle)]
    pub unsafe extern "C" fn os_get_physical_cores() -> c_int {
        cocoa::physical_cores()
    }

    /// # Safety
    /// No pointer arguments.
    #[unsafe(no_mangle)]
    pub unsafe extern "C" fn os_get_logical_cores() -> c_int {
        cocoa::logical_cores()
    }

    /// # Safety
    /// No pointer arguments.
    #[unsafe(no_mangle)]
    pub unsafe extern "C" fn os_get_sys_free_size() -> u64 {
        cocoa::sys_free_size()
    }

    /// # Safety
    /// No pointer arguments.
    #[unsafe(no_mangle)]
    pub unsafe extern "C" fn os_get_sys_total_size() -> u64 {
        cocoa::sys_total_size()
    }

    /// # Safety
    /// `path` is a C string or null. Null returns 0.
    #[unsafe(no_mangle)]
    pub unsafe extern "C" fn os_get_free_space(path: *const c_char) -> i64 {
        cocoa::free_space(cstr(path))
    }

    /// # Safety
    /// `dir` is a C string or null.
    #[unsafe(no_mangle)]
    pub unsafe extern "C" fn os_get_free_disk_space(dir: *const c_char) -> u64 {
        cocoa::free_disk_space(cstr(dir))
    }

    /// # Safety
    /// `usage` is an out-pointer or null. Null returns false.
    #[unsafe(no_mangle)]
    pub unsafe extern "C" fn os_get_proc_memory_usage(usage: *mut os_proc_memory_usage) -> bool {
        if usage.is_null() {
            return false;
        }
        let Some(mem) = cocoa::proc_memory() else {
            return false;
        };
        // SAFETY: `usage` is non-null.
        unsafe {
            (*usage).resident_size = mem.resident;
            (*usage).virtual_size = mem.virtual_size;
        }
        true
    }

    /// # Safety
    /// No pointer arguments.
    #[unsafe(no_mangle)]
    pub unsafe extern "C" fn os_get_proc_resident_size() -> u64 {
        cocoa::proc_resident_size()
    }

    /// # Safety
    /// No pointer arguments.
    #[unsafe(no_mangle)]
    pub unsafe extern "C" fn os_get_proc_virtual_size() -> u64 {
        cocoa::proc_virtual_size()
    }

    /// # Safety
    /// `cfstring` is null or a live `CFStringRef`. The result is
    /// `bmalloc`'d, or null.
    #[unsafe(no_mangle)]
    pub unsafe extern "C" fn cfstr_copy_cstr(
        cfstring: *const c_void,
        encoding: u32,
    ) -> *mut c_char {
        // SAFETY: `cfstring` is null or a live CFString for this call.
        match unsafe { cocoa::copy_cstr(cfstring, encoding) } {
            Some(bytes) => unsafe { alloc_terminated(&bytes) },
            None => ptr::null_mut(),
        }
    }

    /// # Safety
    /// `cfstring` is null or a live `CFStringRef`. `str` is a `dstr` or
    /// null. Null returns false.
    #[unsafe(no_mangle)]
    pub unsafe extern "C" fn cfstr_copy_dstr(
        cfstring: *const c_void,
        encoding: u32,
        str: *mut dstr,
    ) -> bool {
        if str.is_null() {
            return false;
        }
        // SAFETY: `str` is a live `dstr`.
        let mut model = unsafe { dstr_ffi::read_model(str) };
        // SAFETY: `cfstring` is null or a live CFString for this call.
        let ok = unsafe { cocoa::copy_into_dstr(cfstring, encoding, &mut model) };
        // SAFETY: `str` is the same live `dstr`.
        unsafe { dstr_ffi::write_model(str, model) };
        ok
    }
}

#[cfg(unix)]
pub use shims::*;
