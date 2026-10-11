//! Tier 1: safe-API tests mirroring `test/cmocka/test_video_io_inlines.c`,
//! plus edge cases.
//!
//! The C test's values outside the enums have no safe-API counterpart; the
//! conversions from C values are tested here, and what the C-value helpers
//! in `obs_media_io::ffi::video_io` return for them in the Tier 3 parity
//! test.

use obs_c_oracle as _;
use obs_media_io::frame_rate::MediaFramesPerSecond;
use obs_media_io::video_io::{
    VideoColorspace, VideoFormat, VideoRangeType, VideoScaleType, VideoTrc, format_is_yuv,
    resolve_video_range, video_colorspace_from_c, video_colorspace_name, video_format_from_c,
    video_format_name, video_range_name, video_range_type_from_c, video_scale_type_from_c,
    video_scale_type_to_c, video_trc_from_c, video_trc_to_c,
};
use proptest as _;

/// As the C test's `formats_outside`, `colorspaces_outside` and
/// `ranges_outside`, with the last enumerator + 1 written out.
const FORMATS_OUTSIDE: [i32; 3] = [-1, 26, 1000];
const COLORSPACES_OUTSIDE: [i32; 3] = [-1, 6, 1000];
const RANGES_OUTSIDE: [i32; 3] = [-1, 3, 1000];

/// As the C test's `format_cases`: every format, in header order, whether
/// it is YUV, and its name.
const FORMAT_CASES: [(VideoFormat, bool, &str); 26] = [
    (VideoFormat::None, false, "None"),
    (VideoFormat::I420, true, "I420"),
    (VideoFormat::Nv12, true, "NV12"),
    (VideoFormat::Yvyu, true, "YVYU"),
    (VideoFormat::Yuy2, true, "YUY2"),
    (VideoFormat::Uyvy, true, "UYVY"),
    (VideoFormat::Rgba, false, "RGBA"),
    (VideoFormat::Bgra, false, "BGRA"),
    (VideoFormat::Bgrx, false, "BGRX"),
    (VideoFormat::Y800, false, "Y800"),
    (VideoFormat::I444, true, "I444"),
    (VideoFormat::Bgr3, false, "BGR3"),
    (VideoFormat::I422, true, "I422"),
    (VideoFormat::I40a, true, "I40A"),
    (VideoFormat::I42a, true, "I42A"),
    (VideoFormat::Yuva, true, "YUVA"),
    (VideoFormat::Ayuv, true, "AYUV"),
    (VideoFormat::I010, true, "I010"),
    (VideoFormat::P010, true, "P010"),
    (VideoFormat::I210, true, "I210"),
    (VideoFormat::I412, true, "I412"),
    (VideoFormat::Ya2l, true, "YA2L"),
    (VideoFormat::P216, true, "P216"),
    (VideoFormat::P416, true, "P416"),
    // Lower case, characterized, not endorsed.
    (VideoFormat::V210, true, "v210"),
    (VideoFormat::R10l, false, "R10l"),
];

#[test]
fn test_format_is_yuv() {
    assert_eq!(FORMAT_CASES.len(), VideoFormat::ALL.len());
    for (format, yuv, _) in FORMAT_CASES {
        assert_eq!(format_is_yuv(format), yuv, "{format:?}");
    }
}

#[test]
fn test_format_is_yuv_outside_enum() {
    // A shim maps these to None, which is not YUV.
    for value in FORMATS_OUTSIDE {
        assert_eq!(video_format_from_c(value), None, "{value}");
    }
    assert!(!format_is_yuv(VideoFormat::None));
}

#[test]
fn test_get_video_format_name() {
    for (format, _, name) in FORMAT_CASES {
        assert_eq!(video_format_name(format), name, "{format:?}");
    }
}

#[test]
fn test_get_video_format_name_outside_enum() {
    // A shim maps these to None, which is named "None".
    for value in FORMATS_OUTSIDE {
        assert_eq!(video_format_from_c(value), None, "{value}");
    }
    assert_eq!(video_format_name(VideoFormat::None), "None");
}

#[test]
fn test_get_video_colorspace_name() {
    // Default is named as 709.
    assert_eq!(video_colorspace_name(VideoColorspace::Default), "Rec. 709");
    assert_eq!(video_colorspace_name(VideoColorspace::Cs601), "Rec. 601");
    assert_eq!(video_colorspace_name(VideoColorspace::Cs709), "Rec. 709");
    assert_eq!(video_colorspace_name(VideoColorspace::Srgb), "sRGB");
    assert_eq!(
        video_colorspace_name(VideoColorspace::Cs2100Pq),
        "Rec. 2100 (PQ)"
    );
    assert_eq!(
        video_colorspace_name(VideoColorspace::Cs2100Hlg),
        "Rec. 2100 (HLG)"
    );
}

#[test]
fn test_get_video_colorspace_name_outside_enum() {
    // A shim names these "Unknown"; the safe API cannot express them.
    for value in COLORSPACES_OUTSIDE {
        assert_eq!(video_colorspace_from_c(value), None, "{value}");
    }
}

#[test]
fn test_resolve_video_range() {
    for (format, yuv, _) in FORMAT_CASES {
        let resolved = if yuv {
            VideoRangeType::Partial
        } else {
            VideoRangeType::Full
        };
        assert_eq!(
            resolve_video_range(format, VideoRangeType::Default),
            resolved,
            "{format:?}"
        );
        assert_eq!(
            resolve_video_range(format, VideoRangeType::Partial),
            VideoRangeType::Partial,
            "{format:?}"
        );
        assert_eq!(
            resolve_video_range(format, VideoRangeType::Full),
            VideoRangeType::Full,
            "{format:?}"
        );
    }
}

#[test]
fn test_resolve_video_range_outside_enum() {
    // A shim maps formats outside the enum to None, so Default resolves to
    // full, and returns ranges outside the enum unchanged.
    for value in FORMATS_OUTSIDE {
        assert_eq!(video_format_from_c(value), None, "{value}");
    }
    assert_eq!(
        resolve_video_range(VideoFormat::None, VideoRangeType::Default),
        VideoRangeType::Full
    );
    for value in RANGES_OUTSIDE {
        assert_eq!(video_range_type_from_c(value), None, "{value}");
    }
}

#[test]
fn test_get_video_range_name() {
    for (format, yuv, _) in FORMAT_CASES {
        assert_eq!(
            video_range_name(format, VideoRangeType::Default),
            if yuv { "Partial" } else { "Full" },
            "{format:?}"
        );
        assert_eq!(
            video_range_name(format, VideoRangeType::Partial),
            "Partial",
            "{format:?}"
        );
        assert_eq!(
            video_range_name(format, VideoRangeType::Full),
            "Full",
            "{format:?}"
        );
    }
}

#[test]
fn test_get_video_range_name_outside_enum() {
    // As in test_resolve_video_range_outside_enum; a shim names ranges
    // outside the enum "Partial".
    assert_eq!(
        video_range_name(VideoFormat::None, VideoRangeType::Default),
        "Full"
    );
    for value in RANGES_OUTSIDE {
        assert_eq!(video_range_type_from_c(value), None, "{value}");
    }
}

fn fps(numerator: u32, denominator: u32) -> MediaFramesPerSecond {
    MediaFramesPerSecond {
        numerator,
        denominator,
    }
}

#[test]
fn test_frame_rate_conversions() {
    assert_eq!(fps(30, 1).fps(), 30.0);
    assert_eq!(fps(30, 1).frame_interval(), 1.0 / 30.0);

    assert_eq!(fps(30000, 1001).fps(), 30000.0 / 1001.0);
    assert_eq!(fps(30000, 1001).frame_interval(), 1001.0 / 30000.0);

    // The division is in f64, so the full u32 range is exact.
    assert_eq!(fps(u32::MAX, 1).fps(), 4294967295.0);
    assert_eq!(fps(u32::MAX, u32::MAX).fps(), 1.0);
}

#[test]
fn test_frame_rate_zero() {
    // Zero parts are divided as is. Characterized, not endorsed.
    assert_eq!(fps(0, 1).fps(), 0.0);
    assert_eq!(fps(0, 1).frame_interval(), f64::INFINITY);

    assert_eq!(fps(1, 0).fps(), f64::INFINITY);
    assert_eq!(fps(1, 0).frame_interval(), 0.0);

    assert!(fps(0, 0).fps().is_nan());
    assert!(fps(0, 0).frame_interval().is_nan());
}

#[test]
fn test_frame_rate_is_valid() {
    assert!(fps(30, 1).is_valid());
    assert!(fps(30000, 1001).is_valid());
    assert!(fps(u32::MAX, u32::MAX).is_valid());

    assert!(!fps(0, 1).is_valid());
    assert!(!fps(1, 0).is_valid());
    assert!(!fps(0, 0).is_valid());
}

#[test]
fn every_explicit_range_is_kept() {
    for format in VideoFormat::ALL {
        for range in [VideoRangeType::Partial, VideoRangeType::Full] {
            assert_eq!(resolve_video_range(format, range), range, "{format:?}");
        }
    }
}

#[test]
fn range_name_matches_resolved_range() {
    for format in VideoFormat::ALL {
        for range in VideoRangeType::ALL {
            let full = resolve_video_range(format, range) == VideoRangeType::Full;
            assert_eq!(
                video_range_name(format, range),
                if full { "Full" } else { "Partial" },
                "{format:?} {range:?}"
            );
        }
    }
}

#[test]
fn format_names_are_unique() {
    let mut names: Vec<_> = VideoFormat::ALL
        .into_iter()
        .map(video_format_name)
        .collect();
    names.sort_unstable();
    names.dedup();
    assert_eq!(names.len(), VideoFormat::ALL.len());
}

#[test]
fn frame_interval_is_reciprocal_of_fps() {
    for (n, d) in [(1, 1), (24, 1), (25, 1), (60000, 1001), (1, 30)] {
        let f = fps(n, d);
        assert_eq!(f.frame_interval(), f64::from(d) / f64::from(n));
        assert_eq!(f.fps(), f64::from(n) / f64::from(d));
    }
}

#[test]
fn trc_and_scale_type_c_values_round_trip() {
    for trc in VideoTrc::ALL {
        assert_eq!(video_trc_from_c(video_trc_to_c(trc)), Some(trc));
    }
    for scale_type in VideoScaleType::ALL {
        assert_eq!(
            video_scale_type_from_c(video_scale_type_to_c(scale_type)),
            Some(scale_type)
        );
    }
    for value in [-1, 4, 1000] {
        assert_eq!(video_trc_from_c(value), None, "{value}");
    }
    for value in [-1, 5, 1000] {
        assert_eq!(video_scale_type_from_c(value), None, "{value}");
    }
}
