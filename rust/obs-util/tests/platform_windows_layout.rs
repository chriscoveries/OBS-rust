//! Tier 2: the public platform and Windows version structs match the C header.

// The C oracle for platform-windows.c is MSVC-only (obs-c-oracle build.rs).
#![cfg(all(windows, target_env = "msvc"))]

use core::mem::{align_of, offset_of, size_of};

use obs_c_oracle::platform_windows as c;
use obs_util::ffi::platform_windows::{
    os_dirent, os_glob_info, os_globent, os_proc_memory_usage, reg_dword, win_version_info,
};

#[test]
fn os_dirent_layout_matches_c_header() {
    // SAFETY: the oracle layout functions take no arguments.
    unsafe {
        assert_eq!(size_of::<os_dirent>(), c::oracle_os_dirent_size());
        assert_eq!(align_of::<os_dirent>(), c::oracle_os_dirent_align());
        assert_eq!(
            offset_of!(os_dirent, d_name),
            c::oracle_os_dirent_offset_d_name()
        );
        assert_eq!(
            offset_of!(os_dirent, directory),
            c::oracle_os_dirent_offset_directory()
        );
    }
}

#[test]
fn os_globent_layout_matches_c_header() {
    // SAFETY: the oracle layout functions take no arguments.
    unsafe {
        assert_eq!(size_of::<os_globent>(), c::oracle_os_globent_size());
        assert_eq!(align_of::<os_globent>(), c::oracle_os_globent_align());
        assert_eq!(
            offset_of!(os_globent, path),
            c::oracle_os_globent_offset_path()
        );
        assert_eq!(
            offset_of!(os_globent, directory),
            c::oracle_os_globent_offset_directory()
        );
    }
}

#[test]
fn os_glob_info_layout_matches_c_header() {
    // SAFETY: the oracle layout functions take no arguments.
    unsafe {
        assert_eq!(size_of::<os_glob_info>(), c::oracle_os_glob_info_size());
        assert_eq!(align_of::<os_glob_info>(), c::oracle_os_glob_info_align());
        assert_eq!(
            offset_of!(os_glob_info, gl_pathc),
            c::oracle_os_glob_info_offset_gl_pathc()
        );
        assert_eq!(
            offset_of!(os_glob_info, gl_pathv),
            c::oracle_os_glob_info_offset_gl_pathv()
        );
    }
}

#[test]
fn os_proc_memory_usage_layout_matches_c_header() {
    // SAFETY: the oracle layout functions take no arguments.
    unsafe {
        assert_eq!(
            size_of::<os_proc_memory_usage>(),
            c::oracle_os_proc_memory_usage_size()
        );
        assert_eq!(
            align_of::<os_proc_memory_usage>(),
            c::oracle_os_proc_memory_usage_align()
        );
        assert_eq!(
            offset_of!(os_proc_memory_usage, resident_size),
            c::oracle_os_proc_memory_usage_offset_resident_size()
        );
        assert_eq!(
            offset_of!(os_proc_memory_usage, virtual_size),
            c::oracle_os_proc_memory_usage_offset_virtual_size()
        );
    }
}

#[test]
fn win_version_info_layout_matches_c_header() {
    // SAFETY: the oracle layout functions take no arguments.
    unsafe {
        assert_eq!(
            size_of::<win_version_info>(),
            c::oracle_win_version_info_size()
        );
        assert_eq!(
            align_of::<win_version_info>(),
            c::oracle_win_version_info_align()
        );
        assert_eq!(
            offset_of!(win_version_info, major),
            c::oracle_win_version_info_offset_major()
        );
        assert_eq!(
            offset_of!(win_version_info, minor),
            c::oracle_win_version_info_offset_minor()
        );
        assert_eq!(
            offset_of!(win_version_info, build),
            c::oracle_win_version_info_offset_build()
        );
        assert_eq!(
            offset_of!(win_version_info, revis),
            c::oracle_win_version_info_offset_revis()
        );
    }
}

#[test]
fn reg_dword_layout_matches_c_header() {
    // SAFETY: the oracle layout functions take no arguments.
    unsafe {
        assert_eq!(size_of::<reg_dword>(), c::oracle_reg_dword_size());
        assert_eq!(align_of::<reg_dword>(), c::oracle_reg_dword_align());
        assert_eq!(
            offset_of!(reg_dword, status),
            c::oracle_reg_dword_offset_status()
        );
        assert_eq!(
            offset_of!(reg_dword, size),
            c::oracle_reg_dword_offset_size()
        );
        assert_eq!(
            offset_of!(reg_dword, return_value),
            c::oracle_reg_dword_offset_return_value()
        );
    }
}

#[test]
fn win32_struct_sizes_match_the_header() {
    // SAFETY: the oracle size functions take no arguments.
    unsafe {
        assert_eq!(c::oracle_memory_status_ex_size(), 64);
        assert_eq!(c::oracle_process_memory_counters_size(), 72);
        assert_eq!(c::oracle_logical_processor_size(), 32);
    }
}
