//! Safe core for the POSIX time/sleep and directory-enumeration helpers
//! extracted from `libobs/util/platform-nix.c` into
//! `libobs/util/platform-nix-time-dir.c`. The C ABI shim lives in
//! [`crate::ffi::platform_time_dir`]; Windows keeps `platform-windows.c`.
//!
//! Reproduced from the C, including its quirks (each pinned by the parity
//! tests or the cmocka characterization test):
//!
//! - `os_sleepto_ns`/`os_sleepto_ns_fast` return `false` for a past target
//!   without sleeping; `os_sleep_ms` may return early on a signal (no
//!   EINTR resume) while `os_sleepto_ns` resumes `nanosleep` on the
//!   remainder.
//! - `os_readdir` swallows a name of 256+ bytes: the entry is consumed
//!   and the call reports end-of-directory. Untestable on real
//!   filesystems (NAME_MAX is 255), so pinned by code parity only.
//! - `is_dir` follows symlinks via `stat` (not `lstat`/dirent d_type) and
//!   reports `false` with a `LOG_DEBUG` line on stat failure.
//! - `os_glob` ignores its `flags` argument entirely and returns matches
//!   in `glob(3)` order. `gl_pathv[i].path` aliases the libc glob
//!   storage, which `globfree` releases inside `os_globfree` — the same
//!   lifetime the C API documents.
//! - `os_dir` stores the caller's path pointer in C; the Rust `Dir` owns
//!   a copy instead. The pointer only feeds `path + "/" + name` inside
//!   `os_readdir`, so the observable behaviour is identical.

use core::ffi::{c_char, c_int};
use std::ffi::CString;
use std::ptr;

/// Mirrors `struct os_dirent` in `libobs/util/platform.h`.
#[repr(C)]
#[allow(non_camel_case_types)]
#[derive(Debug)]
pub struct os_dirent {
    pub d_name: [c_char; 256],
    pub directory: bool,
}

/// Mirrors `struct os_globent` in `libobs/util/platform.h`.
#[repr(C)]
#[allow(non_camel_case_types)]
#[derive(Debug)]
pub struct os_globent {
    pub path: *mut c_char,
    pub directory: bool,
}

/// Mirrors `struct os_glob_info` in `libobs/util/platform.h`
/// (`typedef os_glob_t`).
#[repr(C)]
#[allow(non_camel_case_types)]
#[derive(Debug)]
pub struct os_glob_info {
    pub gl_pathc: usize,
    pub gl_pathv: *mut os_globent,
}

/// Mirrors `struct posix_glob_info` in the C source: the public header
/// followed by the libc `glob_t` in one allocation, freed together.
#[repr(C)]
struct posix_glob_info {
    base: os_glob_info,
    gl: libc::glob_t,
}

/// `os_gettime_ns` in the C is `!__APPLE__` only, but the shared sleep
/// helpers use it on every OS, so the core keeps it unconditional.
pub fn gettime_ns() -> u64 {
    todo!()
}

/// Mirrors `os_sleepto_ns`: past target returns `false`; `nanosleep`
/// resumes on the remaining time until it returns 0.
pub fn sleepto_ns(time_target: u64) -> bool {
    todo!()
}

/// Mirrors `os_sleepto_ns_fast`: past target returns `false`; loops
/// `usleep` in chunks of at most 999999 µs (remaining rounded up to µs)
/// until `gettime_ns` reaches the target.
pub fn sleepto_ns_fast(time_target: u64) -> bool {
    todo!()
}

/// Mirrors `os_sleep_ms`: a single `usleep`, so a signal may end it early.
pub fn sleep_ms(duration: u32) {
    todo!()
}

/// Mirrors `struct os_dir` from the C source. `path` is an owned copy of
/// the opener's path (see the module docs for why that is invisible).
pub struct Dir {
    dir: ptr::NonNull<libc::DIR>,
    path: CString,
    out: os_dirent,
}

impl Dir {
    /// Mirrors `os_opendir`: `opendir` failure returns `None`.
    pub fn open(path: &[u8]) -> Option<Self> {
        todo!()
    }

    /// Mirrors `os_readdir`, including the 256-byte name cap consumed-
    /// entry quirk. The returned borrow is invalidated by the next call
    /// (the C `dir->out` slot), which the `&mut self` receiver encodes.
    pub fn readdir(&mut self) -> Option<&os_dirent> {
        todo!()
    }
}

impl Drop for Dir {
    /// Mirrors `os_closedir`.
    fn drop(&mut self) {
        todo!()
    }
}

/// The `os_glob` allocation. `base` must stay the first field so an
/// `os_glob_t*` cast lands on it exactly like the C
/// `posix_glob_info`/`os_glob_info` overlay.
pub struct Glob {
    pgi: ptr::NonNull<posix_glob_info>,
}

impl Glob {
    /// Mirrors `os_glob`: `flags` ignored, `glob(3)` itself sorts the
    /// matches and owns the `path` strings; failure yields an error code
    /// and no glob (the C `*pglob = NULL`).
    pub fn open(pattern: &[u8]) -> Result<Self, c_int> {
        todo!()
    }

    /// The `os_glob_t*` view (`&base`), like the C cast of
    /// `posix_glob_info*` to `os_glob_t*`.
    pub fn as_os_glob_t(&self) -> *mut os_glob_info {
        todo!()
    }

    /// Rebuild the handle from an `os_glob_t*` produced by
    /// [`Glob::as_os_glob_t`] (os_globfree's entry point).
    ///
    /// # Safety
    /// `pglob` came from `as_os_glob_t` and is owned by the caller.
    pub unsafe fn from_os_glob_t(pglob: *mut os_glob_info) -> Self {
        todo!()
    }
}

impl Drop for Glob {
    /// Mirrors `os_globfree`: `globfree`, then `bfree` the entry array
    /// and the `posix_glob_info` itself.
    fn drop(&mut self) {
        todo!()
    }
}
