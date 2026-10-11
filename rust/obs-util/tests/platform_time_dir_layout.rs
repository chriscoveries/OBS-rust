//! Tier 2: the `#[repr(C)]` mirrors of `struct os_dirent`,
//! `struct os_globent` and `struct os_glob_info` in
//! `libobs/util/platform.h` match the layout the C compiler produces —
//! callers hand the handles across the boundary, so this layout is the
//! contract. `posix_glob_info` is file-private to
//! `platform-nix-time-dir.c`; its `base`/`gl` overlay is why an
//! `os_glob_t*` cast works, so that is asserted too.

use core::mem::{align_of, offset_of, size_of};

use obs_c_oracle::platform_nix_time_dir as c;
use obs_util::platform_time_dir::{os_dirent, os_glob_info, os_globent};

#[test]
fn os_dirent_layout_matches_platform_h() {
    type E = os_dirent;
    // SAFETY: the oracle layout functions take no arguments and only
    // return constants.
    unsafe {
        assert_eq!(size_of::<E>(), c::oracle_platform_time_dir_os_dirent_size());
        assert_eq!(
            align_of::<E>(),
            c::oracle_platform_time_dir_os_dirent_align()
        );
        assert_eq!(
            offset_of!(E, d_name),
            c::oracle_platform_time_dir_os_dirent_offset_d_name()
        );
        assert_eq!(
            offset_of!(E, directory),
            c::oracle_platform_time_dir_os_dirent_offset_directory()
        );
    }
}

#[test]
fn os_globent_layout_matches_platform_h() {
    type E = os_globent;
    // SAFETY: as above.
    unsafe {
        assert_eq!(
            size_of::<E>(),
            c::oracle_platform_time_dir_os_globent_size()
        );
        assert_eq!(
            align_of::<E>(),
            c::oracle_platform_time_dir_os_globent_align()
        );
        assert_eq!(
            offset_of!(E, path),
            c::oracle_platform_time_dir_os_globent_offset_path()
        );
        assert_eq!(
            offset_of!(E, directory),
            c::oracle_platform_time_dir_os_globent_offset_directory()
        );
    }
}

#[test]
fn os_glob_info_layout_matches_platform_h() {
    type E = os_glob_info;
    // SAFETY: as above.
    unsafe {
        assert_eq!(
            size_of::<E>(),
            c::oracle_platform_time_dir_os_glob_info_size()
        );
        assert_eq!(
            align_of::<E>(),
            c::oracle_platform_time_dir_os_glob_info_align()
        );
        assert_eq!(
            offset_of!(E, gl_pathc),
            c::oracle_platform_time_dir_os_glob_info_offset_gl_pathc()
        );
        assert_eq!(
            offset_of!(E, gl_pathv),
            c::oracle_platform_time_dir_os_glob_info_offset_gl_pathv()
        );
    }
}

/// The `posix_glob_info`/`os_glob_info` overlay: `gl` starts right after
/// `base`, so an `os_glob_t*` lands on `base` exactly like in C — that is
/// what `Glob::from_os_glob_t` casts back on. The Rust twin keeps the
/// same overlay by construction (`#[repr(C)]`, same field order/types).
#[test]
fn posix_glob_info_gl_starts_after_base() {
    // SAFETY: the oracle layout functions only return constants.
    unsafe {
        assert_eq!(
            c::oracle_platform_time_dir_posix_glob_info_offset_gl(),
            c::oracle_platform_time_dir_os_glob_info_size()
        );
    }
}
