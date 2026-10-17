//! Tier 1: safe-API tests mirroring `test/cmocka/test_video_matrices.c`,
//! plus edge cases.
//!
//! The C test's null range pointers and out-of-enum values have no safe-API
//! counterpart; the conversions from C values are tested here, and the
//! pointers in the Tier 3 parity test.

use obs_c_oracle as _;
use obs_media_io::video_io::{
    VideoColorspace, VideoFormat, VideoRangeType, video_colorspace_from_c, video_colorspace_to_c,
    video_format_from_c, video_format_to_c, video_range_type_from_c, video_range_type_to_c,
};
use obs_media_io::video_matrices::{ColorParameters, parameters, parameters_for_format};
use proptest as _;

/// As the C test's `MATRIX_EPSILON`.
const MATRIX_EPSILON: f32 = 1e-5;

fn assert_matrix(actual: &[f32; 16], expected: &[f32; 16]) {
    for (i, (a, e)) in actual.iter().zip(expected).enumerate() {
        assert!(
            (a - e).abs() <= MATRIX_EPSILON,
            "matrix[{i}]: {a}, expected {e}"
        );
    }
}

/// As the C test's `assert_partial_range`.
fn assert_partial_range(p: &ColorParameters, range: f32) {
    let max = range - 1.0;
    let min = 16.0 * range / 256.0 / max;
    assert_eq!(p.range_min, [min; 3]);
    assert_eq!(
        p.range_max,
        [
            235.0 * range / 256.0 / max,
            240.0 * range / 256.0 / max,
            240.0 * range / 256.0 / max,
        ]
    );
}

#[test]
fn test_709_partial_8bit() {
    let p = parameters_for_format(
        VideoColorspace::Cs709,
        VideoRangeType::Partial,
        VideoFormat::Nv12,
    );
    #[rustfmt::skip]
    assert_matrix(&p.matrix, &[
        1.164_383_5, 0.0, 1.792_741_1, -0.972_945_15,
        1.164_383_5, -0.213_248_61, -0.532_909_33, 0.301_482_68,
        1.164_383_5, 2.112_401_7, 0.0, -1.133_402_2,
        0.0, 0.0, 0.0, 1.0,
    ]);
    assert_partial_range(&p, 256.0);
}

#[test]
fn test_709_full_8bit() {
    let p = parameters_for_format(
        VideoColorspace::Cs709,
        VideoRangeType::Full,
        VideoFormat::Nv12,
    );
    #[rustfmt::skip]
    assert_matrix(&p.matrix, &[
        1.0, 0.0, 1.5748, -0.790_487_9,
        1.0, -0.187_324_26, -0.468_124_27, 0.329_009_47,
        1.0, 1.8556, 0.0, -0.931_438_5,
        0.0, 0.0, 0.0, 1.0,
    ]);
    assert_eq!(p.range_min, [0.0; 3]);
    assert_eq!(p.range_max, [1.0; 3]);
}

#[test]
fn test_601_partial_8bit() {
    let p = parameters_for_format(
        VideoColorspace::Cs601,
        VideoRangeType::Partial,
        VideoFormat::Nv12,
    );
    #[rustfmt::skip]
    assert_matrix(&p.matrix, &[
        1.164_383_5, 0.0, 1.596_026_7, -0.874_202_2,
        1.164_383_5, -0.391_762_23, -0.812_967_54, 0.531_667_7,
        1.164_383_5, 2.017_232, 0.0, -1.085_630_7,
        0.0, 0.0, 0.0, 1.0,
    ]);
    assert_partial_range(&p, 256.0);
}

#[test]
fn test_2100_pq_partial_10bit() {
    let p = parameters_for_format(
        VideoColorspace::Cs2100Pq,
        VideoRangeType::Partial,
        VideoFormat::P010,
    );
    #[rustfmt::skip]
    assert_matrix(&p.matrix, &[
        1.167_808_2, 0.0, 1.683_611_4, -0.915_688,
        1.167_808_2, -0.187_877_06, -0.652_337_4, 0.347_458_5,
        1.167_808_2, 2.148_071_5, 0.0, -1.148_145,
        0.0, 0.0, 0.0, 1.0,
    ]);
    assert_partial_range(&p, 1024.0);
}

#[test]
fn test_709_partial_12bit_and_16bit() {
    let p = parameters_for_format(
        VideoColorspace::Cs709,
        VideoRangeType::Partial,
        VideoFormat::I412,
    );
    #[rustfmt::skip]
    assert_matrix(&p.matrix, &[
        1.168_664_3, 0.0, 1.799_332, -0.972_945_15,
        1.168_664_3, -0.214_032_62, -0.534_868_5, 0.301_482_65,
        1.168_664_3, 2.120_168, 0.0, -1.133_402_2,
        0.0, 0.0, 0.0, 1.0,
    ]);
    assert_partial_range(&p, 4096.0);

    let p = parameters_for_format(
        VideoColorspace::Cs709,
        VideoRangeType::Partial,
        VideoFormat::P216,
    );
    #[rustfmt::skip]
    assert_matrix(&p.matrix, &[
        1.168_932, 0.0, 1.799_743_9, -0.972_945,
        1.168_932, -0.214_081_62, -0.534_990_97, 0.301_482_65,
        1.168_932, 2.120_653_4, 0.0, -1.133_402_2,
        0.0, 0.0, 0.0, 1.0,
    ]);
    assert_partial_range(&p, 65536.0);
}

#[test]
fn test_colorspace_aliases() {
    for range in [VideoRangeType::Partial, VideoRangeType::Full] {
        let cs_709 = parameters_for_format(VideoColorspace::Cs709, range, VideoFormat::Nv12);
        for cs in [VideoColorspace::Default, VideoColorspace::Srgb] {
            assert_eq!(
                parameters_for_format(cs, range, VideoFormat::Nv12),
                cs_709,
                "{cs:?} {range:?}"
            );
        }

        let cs_pq = parameters_for_format(VideoColorspace::Cs2100Pq, range, VideoFormat::P010);
        assert_eq!(
            parameters_for_format(VideoColorspace::Cs2100Hlg, range, VideoFormat::P010),
            cs_pq,
            "{range:?}"
        );
    }
}

#[test]
fn test_unknown_colorspace() {
    // The shim returns false for a C value with no colorspace.
    for value in [
        -1,
        video_colorspace_to_c(VideoColorspace::Cs2100Hlg) + 1,
        100,
    ] {
        assert_eq!(video_colorspace_from_c(value), None, "{value}");
    }
}

#[test]
fn test_only_full_range_is_full() {
    let partial = parameters_for_format(
        VideoColorspace::Cs709,
        VideoRangeType::Partial,
        VideoFormat::Nv12,
    );
    let full = parameters_for_format(
        VideoColorspace::Cs709,
        VideoRangeType::Full,
        VideoFormat::Nv12,
    );
    assert_ne!(partial.matrix, full.matrix);

    // Characterized, not endorsed: VIDEO_RANGE_DEFAULT is partial range.
    assert_eq!(
        parameters_for_format(
            VideoColorspace::Cs709,
            VideoRangeType::Default,
            VideoFormat::Nv12
        ),
        partial
    );
    // The shim converts C values outside the enum to Partial.
    for value in [-1, 3, 100] {
        assert_eq!(video_range_type_from_c(value), None, "{value}");
    }
}

#[test]
fn test_format_bit_depths() {
    let ten = [
        VideoFormat::I010,
        VideoFormat::P010,
        VideoFormat::I210,
        VideoFormat::V210,
        VideoFormat::R10l,
    ];
    let twelve = [VideoFormat::I412, VideoFormat::Ya2l];
    let sixteen = [VideoFormat::P216, VideoFormat::P416];

    for format in VideoFormat::ALL {
        let (bits, range) = if ten.contains(&format) {
            (10, 1024.0)
        } else if twelve.contains(&format) {
            (12, 4096.0)
        } else if sixteen.contains(&format) {
            (16, 65536.0)
        } else {
            (8, 256.0)
        };
        assert_eq!(format.bits_per_channel(), bits, "{format:?}");
        let p = parameters_for_format(VideoColorspace::Cs709, VideoRangeType::Partial, format);
        assert_partial_range(&p, range);
    }

    // The shim converts C values outside the enum to None, which is 8 bits.
    for value in [-1, 26, 100] {
        assert_eq!(video_format_from_c(value), None, "{value}");
    }
    assert_eq!(VideoFormat::None.bits_per_channel(), 8);
}

#[test]
fn test_get_parameters_bit_depth() {
    for range in [VideoRangeType::Partial, VideoRangeType::Full] {
        for cs in [
            VideoColorspace::Default,
            VideoColorspace::Cs601,
            VideoColorspace::Cs709,
            VideoColorspace::Srgb,
        ] {
            assert_eq!(
                parameters(cs, range),
                parameters_for_format(cs, range, VideoFormat::Nv12),
                "{cs:?} {range:?}"
            );
        }
        for cs in [VideoColorspace::Cs2100Pq, VideoColorspace::Cs2100Hlg] {
            assert_eq!(
                parameters(cs, range),
                parameters_for_format(cs, range, VideoFormat::P010),
                "{cs:?} {range:?}"
            );
        }
    }
}

#[test]
fn every_matrix_has_an_identity_last_row() {
    for cs in VideoColorspace::ALL {
        for range in VideoRangeType::ALL {
            for format in VideoFormat::ALL {
                let p = parameters_for_format(cs, range, format);
                assert_eq!(
                    p.matrix[12..],
                    [0.0, 0.0, 0.0, 1.0],
                    "{cs:?} {range:?} {format:?}"
                );
            }
        }
    }
}

#[test]
fn round_trip_through_c_values() {
    for cs in VideoColorspace::ALL {
        assert_eq!(video_colorspace_from_c(video_colorspace_to_c(cs)), Some(cs));
    }
    for format in VideoFormat::ALL {
        assert_eq!(video_format_from_c(video_format_to_c(format)), Some(format));
    }
    for range in VideoRangeType::ALL {
        assert_eq!(
            video_range_type_from_c(video_range_type_to_c(range)),
            Some(range)
        );
    }
}
