//! C ABI shims for `libobs/util/platform-nix-time-dir.c`. The thin
//! wrappers convert raw C pointers/strings into the safe
//! `crate::platform_time_dir` API — null checks, `CStr` reads, and the
//! `os_dir`/`posix_glob_info` handle round-trips that the C contract
//! encodes.
//!
//! The whole surface is Unix-only (`#[cfg(unix)]`): on Windows the same
//! symbols come from `util/platform-windows.c`, which stays in C.
//! `os_gettime_ns` is additionally `!__APPLE__` in the C source (macOS
//! implements it in `platform-cocoa.m`), so its shim is
//! `#[cfg(not(target_os = "macos"))]` to keep the symbol set identical
//! per-OS.

#[cfg(unix)]
pub use unix::*;

#[cfg(unix)]
mod unix {
    use core::ffi::{c_char, c_int};

    use crate::platform_time_dir::{
        self, Dir, Glob, os_dirent, os_glob_info, sleep_ms, sleepto_ns, sleepto_ns_fast,
    };

    /// # Safety
    /// `path` is a NUL-terminated C string that outlives the call (the C
    /// `os_dir` keeps the caller's pointer; the Rust `Dir` copies it).
    #[unsafe(no_mangle)]
    pub unsafe extern "C" fn os_opendir(path: *const c_char) -> *mut Dir {
        if path.is_null() {
            return core::ptr::null_mut();
        }
        // SAFETY: `path` is NUL-terminated per the function contract.
        let path = unsafe { core::ffi::CStr::from_ptr(path) }.to_bytes();
        match Dir::open(path) {
            Some(dir) => Box::into_raw(Box::new(dir)),
            None => core::ptr::null_mut(),
        }
    }

    /// # Safety
    /// `dir` is a live `os_dir_t` from `os_opendir`, or NULL.
    #[unsafe(no_mangle)]
    pub unsafe extern "C" fn os_readdir(dir: *mut Dir) -> *mut os_dirent {
        if dir.is_null() {
            return core::ptr::null_mut();
        }
        // SAFETY: `dir` is a live `os_dir_t` per the contract.
        let dir = unsafe { &mut *dir };
        match dir.readdir() {
            Some(ent) => ent as *mut os_dirent,
            None => core::ptr::null_mut(),
        }
    }

    /// # Safety
    /// `dir` is an `os_dir_t` from `os_opendir` (owned by the caller),
    /// or NULL.
    #[unsafe(no_mangle)]
    pub unsafe extern "C" fn os_closedir(dir: *mut Dir) {
        if !dir.is_null() {
            // SAFETY: `dir` came from `os_opendir` per the contract.
            drop(unsafe { Box::from_raw(dir) });
        }
    }

    /// # Safety
    /// `pattern` is a NUL-terminated C string; `pglob` is a valid out
    /// pointer. `flags` is ignored exactly as in the C
    /// (`UNUSED_PARAMETER(flags)`).
    #[unsafe(no_mangle)]
    pub unsafe extern "C" fn os_glob(
        pattern: *const c_char,
        _flags: c_int,
        pglob: *mut *mut os_glob_info,
    ) -> c_int {
        if pattern.is_null() || pglob.is_null() {
            return libc::GLOB_NOSPACE;
        }
        // SAFETY: `pattern` is NUL-terminated; `pglob` is a valid out ptr.
        let pattern = unsafe { core::ffi::CStr::from_ptr(pattern) }.to_bytes();
        match Glob::open(pattern) {
            Ok(glob) => {
                let out = glob.as_os_glob_t();
                core::mem::forget(glob);
                unsafe { *pglob = out };
                0
            }
            Err(code) => {
                unsafe { *pglob = core::ptr::null_mut() };
                code
            }
        }
    }

    /// # Safety
    /// `pglob` is an `os_glob_t` from `os_glob` (owned by the caller), or
    /// NULL.
    #[unsafe(no_mangle)]
    pub unsafe extern "C" fn os_globfree(pglob: *mut os_glob_info) {
        if !pglob.is_null() {
            // SAFETY: `pglob` came from `os_glob` per the contract.
            drop(unsafe { Glob::from_os_glob_t(pglob) });
        }
    }

    /// # Safety
    /// `time_target` is a CLOCK_MONOTONIC-based nanosecond deadline.
    #[unsafe(no_mangle)]
    pub unsafe extern "C" fn os_sleepto_ns(time_target: u64) -> bool {
        sleepto_ns(time_target)
    }

    /// # Safety
    /// `time_target` is a CLOCK_MONOTONIC-based nanosecond deadline.
    #[unsafe(no_mangle)]
    pub unsafe extern "C" fn os_sleepto_ns_fast(time_target: u64) -> bool {
        sleepto_ns_fast(time_target)
    }

    /// # Safety
    /// `duration` is a millisecond count.
    #[unsafe(no_mangle)]
    pub unsafe extern "C" fn os_sleep_ms(duration: u32) {
        sleep_ms(duration)
    }

    /// `!__APPLE__` in the C source — macOS keeps `platform-cocoa.m`'s
    /// `os_gettime_ns`, so the shim only exists on non-Apple POSIX.
    ///
    /// # Safety
    /// No inputs; returns a CLOCK_MONOTONIC nanosecond timestamp.
    #[cfg(not(target_os = "macos"))]
    #[unsafe(no_mangle)]
    pub unsafe extern "C" fn os_gettime_ns() -> u64 {
        platform_time_dir::gettime_ns()
    }
}
