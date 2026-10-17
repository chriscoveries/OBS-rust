//! Tier 2: the public platform structs match the C header.

#![cfg(unix)]

use core::mem::{align_of, offset_of, size_of};

use obs_c_oracle::platform_nix as c;
use obs_util::ffi::platform_nix::{os_dirent, os_glob_info, os_globent, os_proc_memory_usage};

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
