//! Oracle bindings for `libobs/util/platform-nix-time-dir.c` — every
//! symbol renamed `oracle_*` inside `oracle/platform_nix_time_dir_posix.c`.
//! Opaque handle types mirror the file-private `struct os_dir` and
//! `struct posix_glob_info`; only pointers cross the boundary.

use core::ffi::{c_char, c_int};

/// `struct os_dirent` from `libobs/util/platform.h` (public ABI).
#[repr(C)]
#[allow(non_camel_case_types)]
#[derive(Debug)]
pub struct oracle_os_dirent {
    pub d_name: [c_char; 256],
    pub directory: bool,
}

/// `struct os_globent` from `libobs/util/platform.h`.
#[repr(C)]
#[allow(non_camel_case_types)]
#[derive(Debug)]
pub struct oracle_os_globent {
    pub path: *mut c_char,
    pub directory: bool,
}

/// `struct os_glob_info` (`os_glob_t`) from `libobs/util/platform.h`.
#[repr(C)]
#[allow(non_camel_case_types)]
#[derive(Debug)]
pub struct oracle_os_glob_info {
    pub gl_pathc: usize,
    pub gl_pathv: *mut oracle_os_globent,
}

/// Opaque `struct os_dir`. Only pointers cross the boundary.
pub enum OracleDir {}

/// Opaque `struct posix_glob_info`. Only pointers cross the boundary.
pub enum OracleGlob {}

#[cfg(unix)]
unsafe extern "C" {
    pub fn oracle_os_opendir(path: *const c_char) -> *mut OracleDir;
    pub fn oracle_os_readdir(dir: *mut OracleDir) -> *mut oracle_os_dirent;
    pub fn oracle_os_closedir(dir: *mut OracleDir);
    pub fn oracle_os_glob(
        pattern: *const c_char,
        flags: c_int,
        pglob: *mut *mut oracle_os_glob_info,
    ) -> c_int;
    pub fn oracle_os_globfree(pglob: *mut oracle_os_glob_info);
    pub fn oracle_os_sleepto_ns(time_target: u64) -> bool;
    pub fn oracle_os_sleepto_ns_fast(time_target: u64) -> bool;
    pub fn oracle_os_sleep_ms(duration: u32);
    #[cfg(not(target_os = "macos"))]
    pub fn oracle_os_gettime_ns() -> u64;

    pub fn oracle_platform_time_dir_os_dirent_size() -> usize;
    pub fn oracle_platform_time_dir_os_dirent_align() -> usize;
    pub fn oracle_platform_time_dir_os_dirent_offset_d_name() -> usize;
    pub fn oracle_platform_time_dir_os_dirent_offset_directory() -> usize;
    pub fn oracle_platform_time_dir_os_globent_size() -> usize;
    pub fn oracle_platform_time_dir_os_globent_align() -> usize;
    pub fn oracle_platform_time_dir_os_globent_offset_path() -> usize;
    pub fn oracle_platform_time_dir_os_globent_offset_directory() -> usize;
    pub fn oracle_platform_time_dir_os_glob_info_size() -> usize;
    pub fn oracle_platform_time_dir_os_glob_info_align() -> usize;
    pub fn oracle_platform_time_dir_os_glob_info_offset_gl_pathc() -> usize;
    pub fn oracle_platform_time_dir_os_glob_info_offset_gl_pathv() -> usize;
    pub fn oracle_platform_time_dir_posix_glob_info_size() -> usize;
    pub fn oracle_platform_time_dir_posix_glob_info_offset_gl() -> usize;
}
