//! Tier 3: the safe cores of the `static inline` helpers in
//! `libobs/media-io/video-io.h` and `libobs/media-io/frame-rate.h`, and the
//! C-value helpers in `obs_media_io::ffi::video_io`, behave exactly like
//! the header functions, compiled as non-inline oracle wrappers.
//!
//! No intentional differences from C. The frame-rate conversions are
//! single `f64` divisions and are compared bit for bit (NaN included).
//!
//! The enum inputs are small, so besides proptest over arbitrary `c_int`s
//! the tests sweep every enumerator plus values just outside each enum.
//! The sweep covers every input of `test/cmocka/test_video_io_inlines.c`.
//!
//! Reminder: the mutation check (break the core, see this file fail) is done
//! once per port, not on every change.

use core::ffi::{CStr, c_char, c_int};

use obs_c_oracle::frame_rate::{self as fr, OracleMediaFramesPerSecond};
use obs_c_oracle::video_io_inlines as c;
use obs_media_io::ffi::frame_rate::media_frames_per_second;
use obs_media_io::ffi::video_io as rs;
use obs_media_io::frame_rate::MediaFramesPerSecond;
use obs_media_io::video_io::{
    self, VideoColorspace, VideoFormat, VideoRangeType, video_colorspace_to_c, video_format_to_c,
    video_range_type_to_c,
};
use proptest::prelude::*;

/// Every enumerator of an enum with `count` values, plus two values on each
/// side, and the extremes.
fn sweep(count: usize) -> impl Iterator<Item = c_int> {
    let last = c_int::try_from(count).unwrap() - 1;
    (-2..=last + 2).chain([c_int::MIN, c_int::MAX, 1000])
}

fn formats() -> impl Iterator<Item = c_int> {
    sweep(VideoFormat::ALL.len())
}

fn colorspaces() -> impl Iterator<Item = c_int> {
    sweep(VideoColorspace::ALL.len())
}

fn ranges() -> impl Iterator<Item = c_int> {
    sweep(VideoRangeType::ALL.len())
}

/// The oracle's returned string.
///
/// # Safety
///
/// `s` must be a C string that outlives the program (the header's literals).
unsafe fn oracle_str(s: *const c_char) -> &'static str {
    // SAFETY: guaranteed by the caller.
    unsafe { CStr::from_ptr(s) }.to_str().unwrap()
}

fn check_format(format: c_int) {
    // SAFETY: the oracle wrappers take any int and return a header literal.
    unsafe {
        assert_eq!(
            rs::format_is_yuv(format),
            c::oracle_format_is_yuv(format),
            "format_is_yuv({format})"
        );
        assert_eq!(
            rs::get_video_format_name(format),
            oracle_str(c::oracle_get_video_format_name(format)),
            "get_video_format_name({format})"
        );
    }
}

fn check_colorspace(cs: c_int) {
    // SAFETY: as in check_format.
    unsafe {
        assert_eq!(
            rs::get_video_colorspace_name(cs),
            oracle_str(c::oracle_get_video_colorspace_name(cs)),
            "get_video_colorspace_name({cs})"
        );
    }
}

fn check_range(format: c_int, range: c_int) {
    // SAFETY: as in check_format.
    unsafe {
        assert_eq!(
            rs::resolve_video_range(format, range),
            c::oracle_resolve_video_range(format, range),
            "resolve_video_range({format}, {range})"
        );
        assert_eq!(
            rs::get_video_range_name(format, range),
            oracle_str(c::oracle_get_video_range_name(format, range)),
            "get_video_range_name({format}, {range})"
        );
    }
}

#[test]
fn c_value_helpers_match_c_over_sweep() {
    for format in formats() {
        check_format(format);
        for range in ranges() {
            check_range(format, range);
        }
    }
    for cs in colorspaces() {
        check_colorspace(cs);
    }
}

#[test]
fn safe_core_matches_c_for_every_enumerator() {
    // SAFETY: as in check_format.
    unsafe {
        for format in VideoFormat::ALL {
            let f = video_format_to_c(format);
            assert_eq!(
                video_io::format_is_yuv(format),
                c::oracle_format_is_yuv(f),
                "{format:?}"
            );
            assert_eq!(
                video_io::video_format_name(format),
                oracle_str(c::oracle_get_video_format_name(f)),
                "{format:?}"
            );
            for range in VideoRangeType::ALL {
                let r = video_range_type_to_c(range);
                assert_eq!(
                    video_range_type_to_c(video_io::resolve_video_range(format, range)),
                    c::oracle_resolve_video_range(f, r),
                    "{format:?} {range:?}"
                );
                assert_eq!(
                    video_io::video_range_name(format, range),
                    oracle_str(c::oracle_get_video_range_name(f, r)),
                    "{format:?} {range:?}"
                );
            }
        }
        for cs in VideoColorspace::ALL {
            assert_eq!(
                video_io::video_colorspace_name(cs),
                oracle_str(c::oracle_get_video_colorspace_name(video_colorspace_to_c(
                    cs
                ))),
                "{cs:?}"
            );
        }
    }
}

/// Compares the safe core with the oracle for one frame rate, bit for bit.
fn check_fps(numerator: u32, denominator: u32) {
    let safe = MediaFramesPerSecond::from(media_frames_per_second {
        numerator,
        denominator,
    });
    let oracle = OracleMediaFramesPerSecond {
        numerator,
        denominator,
    };
    // SAFETY: the oracle wrappers take the struct by value and only compute.
    unsafe {
        assert_eq!(
            safe.frame_interval().to_bits(),
            fr::oracle_media_frames_per_second_to_frame_interval(oracle).to_bits(),
            "frame_interval({numerator}/{denominator})"
        );
        assert_eq!(
            safe.fps().to_bits(),
            fr::oracle_media_frames_per_second_to_fps(oracle).to_bits(),
            "fps({numerator}/{denominator})"
        );
        assert_eq!(
            safe.is_valid(),
            fr::oracle_media_frames_per_second_is_valid(oracle),
            "is_valid({numerator}/{denominator})"
        );
    }
}

#[test]
fn frame_rate_matches_c_at_edges() {
    let parts = [
        0,
        1,
        2,
        24,
        25,
        30,
        1001,
        30000,
        60000,
        u32::MAX - 1,
        u32::MAX,
    ];
    for n in parts {
        for d in parts {
            check_fps(n, d);
        }
    }
}

proptest! {
    #[test]
    fn format_helpers_match_c(format in any::<c_int>()) {
        check_format(format);
    }

    #[test]
    fn colorspace_name_matches_c(cs in any::<c_int>()) {
        check_colorspace(cs);
    }

    #[test]
    fn range_helpers_match_c(format in any::<c_int>(), range in any::<c_int>()) {
        check_range(format, range);
    }

    #[test]
    fn frame_rate_matches_c(numerator in any::<u32>(), denominator in any::<u32>()) {
        check_fps(numerator, denominator);
    }
}
