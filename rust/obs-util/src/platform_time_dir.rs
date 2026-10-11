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
use std::ffi::{CStr, CString};
use std::ptr;

mod sys {
    //! Non-libc externs the POSIX syscalls cannot express (`blog` comes
    //! from `util/base-variadic.c`, always compiled).
    use core::ffi::{c_char, c_int};

    /// `LOG_DEBUG` in `libobs/util/base.h`.
    pub const LOG_DEBUG: c_int = 400;

    unsafe extern "C" {
        pub fn blog(log_level: c_int, format: *const c_char, ...);
    }
}

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

/// Mirrors `is_dir`: `stat` (follows symlinks); a failed stat logs and
/// reports `false`.
fn is_dir(path: &[u8]) -> bool {
    let c_path = CString::new(path).expect("interior NUL cannot come from a C string");
    let mut st: libc::stat = unsafe { core::mem::zeroed() };
    // SAFETY: `c_path` is NUL-terminated and `st` is a valid out struct.
    if unsafe { libc::stat(c_path.as_ptr(), &raw mut st) } == 0 {
        return st.st_mode & libc::S_IFMT == libc::S_IFDIR;
    }
    let errno = std::io::Error::last_os_error().raw_os_error().unwrap_or(0);
    // SAFETY: format string is static; args match its `%s`/`%d`.
    unsafe {
        sys::blog(
            sys::LOG_DEBUG,
            c"is_dir: stat for %s failed, errno: %d".as_ptr(),
            c_path.as_ptr(),
            errno,
        )
    };
    false
}

/// `os_gettime_ns` in the C is `!__APPLE__` only, but the shared sleep
/// helpers use it on every OS, so the core keeps it unconditional.
pub fn gettime_ns() -> u64 {
    let mut ts: libc::timespec = unsafe { core::mem::zeroed() };
    // SAFETY: `ts` is a valid out struct.
    unsafe { libc::clock_gettime(libc::CLOCK_MONOTONIC, &raw mut ts) };
    (ts.tv_sec as u64) * 1_000_000_000 + ts.tv_nsec as u64
}

/// Mirrors `os_sleepto_ns`: past target returns `false`; `nanosleep`
/// resumes on the remaining time until it returns 0.
pub fn sleepto_ns(time_target: u64) -> bool {
    let current = gettime_ns();
    if time_target < current {
        return false;
    }
    let remaining = time_target - current;
    let mut req = libc::timespec {
        tv_sec: (remaining / 1_000_000_000) as libc::time_t,
        tv_nsec: (remaining % 1_000_000_000) as libc::c_long,
    };
    let mut remain: libc::timespec = unsafe { core::mem::zeroed() };
    // SAFETY: `req`/`remain` are valid timespecs owned by this call.
    while unsafe { libc::nanosleep(&raw const req, &raw mut remain) } != 0 {
        req = remain;
        remain = unsafe { core::mem::zeroed() };
    }
    true
}

/// Mirrors `os_sleepto_ns_fast`: past target returns `false`; loops
/// `usleep` in chunks of at most 999999 µs (remaining rounded up to µs)
/// until `gettime_ns` reaches the target.
pub fn sleepto_ns_fast(time_target: u64) -> bool {
    let mut current = gettime_ns();
    if time_target < current {
        return false;
    }
    loop {
        let remain_us = (time_target - current).div_ceil(1000);
        let us: libc::useconds_t = if remain_us >= 1_000_000 {
            999_999
        } else {
            remain_us as libc::useconds_t
        };
        // SAFETY: `us` is a valid microsecond count.
        unsafe { libc::usleep(us) };
        current = gettime_ns();
        if time_target <= current {
            return true;
        }
    }
}

/// Mirrors `os_sleep_ms`: a single `usleep`, so a signal may end it
/// early. `duration * 1000` wraps mod 2^32 exactly as in C.
pub fn sleep_ms(duration: u32) {
    // SAFETY: `usleep` takes a plain value.
    unsafe { libc::usleep(duration.wrapping_mul(1000)) };
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
        let path = CString::new(path).expect("interior NUL cannot come from a C string");
        // SAFETY: `path` is NUL-terminated.
        let dir = unsafe { libc::opendir(path.as_ptr()) };
        let dir = ptr::NonNull::new(dir)?;
        Some(Self {
            dir,
            path,
            out: os_dirent {
                d_name: [0; 256],
                directory: false,
            },
        })
    }

    /// Mirrors `os_readdir`, including the 256-byte name cap consumed-
    /// entry quirk. The returned borrow is invalidated by the next call
    /// (the C `dir->out` slot), which the `&mut self` receiver encodes.
    /// `&mut` matches the C callers' writable `struct os_dirent *`.
    pub fn readdir(&mut self) -> Option<&mut os_dirent> {
        // SAFETY: `self.dir` is a live DIR opened by `open`.
        let ent = unsafe { libc::readdir(self.dir.as_ptr()) };
        let ent = ptr::NonNull::new(ent)?;
        // SAFETY: `d_name` inside `dirent` is NUL-terminated per POSIX.
        let name = unsafe { CStr::from_ptr((*ent.as_ptr()).d_name.as_ptr()) }.to_bytes();
        if name.len() >= self.out.d_name.len() {
            return None;
        }
        // C `memcpy(out.d_name, d_name, length + 1)`: bytes past the NUL
        // keep the previous entry's tail, so we copy name+NUL only.
        for (dst, &src) in self.out.d_name.iter_mut().zip(name.iter().chain(&[0])) {
            *dst = src as c_char;
        }
        let mut file_path = Vec::with_capacity(self.path.as_bytes().len() + 1 + name.len());
        file_path.extend_from_slice(self.path.as_bytes());
        file_path.push(b'/');
        file_path.extend_from_slice(name);
        self.out.directory = is_dir(&file_path);
        Some(&mut self.out)
    }
}

impl Drop for Dir {
    /// Mirrors `os_closedir`.
    fn drop(&mut self) {
        // SAFETY: `self.dir` is a live DIR owned by `self`.
        unsafe { libc::closedir(self.dir.as_ptr()) };
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
        let pattern = CString::new(pattern).expect("interior NUL cannot come from a C string");
        // SAFETY: `pgi` starts zeroed like the C stack `= {0}` struct;
        // `glob` fills `pgi.gl`, and on success we write `pgi.base`
        // before handing it out.
        let mut pgi: posix_glob_info = unsafe { core::mem::zeroed() };
        let ret = unsafe { libc::glob(pattern.as_ptr(), 0, None, &raw mut pgi.gl) };
        if ret != 0 {
            return Err(ret);
        }
        let pathc = pgi.gl.gl_pathc;
        // SAFETY: `gl_pathv` holds `gl_pathc` C strings per POSIX.
        let paths = unsafe { core::slice::from_raw_parts(pgi.gl.gl_pathv, pathc) };
        let ents = if pathc == 0 {
            ptr::null_mut()
        } else {
            let ents = unsafe {
                crate::ffi::darray::bmalloc(pathc * size_of::<os_globent>()) as *mut os_globent
            };
            if ents.is_null() {
                // SAFETY: `pgi.gl` was filled by `glob` above.
                unsafe { libc::globfree(&raw mut pgi.gl) };
                return Err(libc::GLOB_NOSPACE);
            }
            ents
        };
        for (i, &path) in paths.iter().enumerate() {
            // SAFETY: `ents` has `pathc` slots; `path` is a C string.
            unsafe {
                (*ents.add(i)).path = path;
                (*ents.add(i)).directory = is_dir(CStr::from_ptr(path).to_bytes());
            }
        }
        pgi.base.gl_pathc = pathc;
        pgi.base.gl_pathv = ents;
        // SAFETY: `pgi` is fully initialized; `bmemdup` copies it and we
        // own the copy (freed in `drop`), like the C `bmemdup(&pgi, …)`.
        let heap = unsafe {
            crate::ffi::bmem::bmemdup(
                &raw const pgi as *const core::ffi::c_void,
                size_of::<posix_glob_info>(),
            )
        } as *mut posix_glob_info;
        let Some(heap) = ptr::NonNull::new(heap) else {
            // SAFETY: release what `open` allocated before reporting.
            unsafe {
                libc::globfree(&raw mut pgi.gl);
                crate::ffi::darray::bfree(ents as *mut core::ffi::c_void);
            }
            return Err(libc::GLOB_NOSPACE);
        };
        Ok(Self { pgi: heap })
    }

    /// The `os_glob_t*` view (`&base`), like the C cast of
    /// `posix_glob_info*` to `os_glob_t*`.
    pub fn as_os_glob_t(&self) -> *mut os_glob_info {
        // SAFETY: `pgi` is a live allocation owned by `self`; `&raw`
        // forms the pointer without reading, like the C `&pgi->base`.
        unsafe { &raw const (*self.pgi.as_ptr()).base as *mut os_glob_info }
    }

    /// Rebuild the handle from an `os_glob_t*` produced by
    /// [`Glob::as_os_glob_t`] (os_globfree's entry point).
    ///
    /// # Safety
    /// `pglob` came from `as_os_glob_t` and is owned by the caller.
    pub unsafe fn from_os_glob_t(pglob: *mut os_glob_info) -> Self {
        Self {
            pgi: ptr::NonNull::new(pglob as *mut posix_glob_info).expect("non-null os_glob_t"),
        }
    }
}

impl Drop for Glob {
    /// Mirrors `os_globfree`: `globfree`, then `bfree` the entry array
    /// and the `posix_glob_info` itself.
    fn drop(&mut self) {
        // SAFETY: `pgi`/`gl`/`gl_pathv` are the allocations `open` made.
        unsafe {
            libc::globfree(&raw mut (*self.pgi.as_ptr()).gl);
            crate::ffi::darray::bfree((*self.pgi.as_ptr()).base.gl_pathv as *mut core::ffi::c_void);
            crate::ffi::darray::bfree(self.pgi.as_ptr() as *mut core::ffi::c_void);
        }
    }
}
