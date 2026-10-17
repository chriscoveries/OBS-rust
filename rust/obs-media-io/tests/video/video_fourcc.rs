//! Tier 1: safe-API tests mirroring `test/cmocka/test_video_fourcc.c`, plus
//! edge cases.

use obs_c_oracle as _;
use obs_media_io::video_fourcc::make_fourcc;
use obs_media_io::video_io::VideoFormat;
use proptest as _;

fn assert_codes(codes: &[&[u8; 4]], expected: VideoFormat) {
    for code in codes {
        assert_eq!(
            VideoFormat::from_fourcc(make_fourcc(**code)),
            expected,
            "fourcc {:?}",
            String::from_utf8_lossy(*code)
        );
    }
}

#[test]
fn test_uyvy_codes() {
    assert_codes(
        &[
            b"UYVY", b"HDYC", b"UYNV", b"UYNY", b"uyv1", b"2vuy", b"2Vuy",
        ],
        VideoFormat::Uyvy,
    );
}

#[test]
fn test_yuy2_codes() {
    assert_codes(
        &[
            b"YUY2", b"Y422", b"V422", b"VYUY", b"YUNV", b"yuv2", b"yuvs",
        ],
        VideoFormat::Yuy2,
    );
}

#[test]
fn test_yvyu_and_y800_codes() {
    assert_eq!(
        VideoFormat::from_fourcc(make_fourcc(*b"YVYU")),
        VideoFormat::Yvyu
    );
    assert_eq!(
        VideoFormat::from_fourcc(make_fourcc(*b"Y800")),
        VideoFormat::Y800
    );
}

#[test]
fn test_unknown_codes() {
    assert_eq!(VideoFormat::from_fourcc(0), VideoFormat::None);
    assert_eq!(VideoFormat::from_fourcc(u32::MAX), VideoFormat::None);

    // Characterized, not endorsed: fourccs of other formats the enum has are
    // not mapped.
    assert_codes(
        &[
            b"I420", b"NV12", b"RGBA", b"BGRA", b"I444", b"P010", b"v210", b"AYUV",
        ],
        VideoFormat::None,
    );
}

#[test]
fn test_case_sensitive() {
    assert_codes(
        &[
            b"uyvy", b"yuy2", b"yvyu", b"y800", b"2VUY", b"UYV1", b"YUV2", b"YUVS",
        ],
        VideoFormat::None,
    );
}

#[test]
fn test_byte_order() {
    // The first character is the low byte. Read the other way round, "UYVY"
    // is "YVYU" and "YUY2" is "2YUY".
    assert_eq!(VideoFormat::from_fourcc(0x5559_5659), VideoFormat::Yvyu);
    assert_eq!(VideoFormat::from_fourcc(0x5955_5932), VideoFormat::None);
}

#[test]
fn make_fourcc_packs_first_character_low() {
    assert_eq!(make_fourcc(*b"UYVY"), 0x5956_5955);
    assert_eq!(make_fourcc([0, 0, 0, 0xff]), 0xff00_0000);
}

#[test]
fn mapped_codes_need_all_four_bytes() {
    // A prefix of a mapped code, padded with NUL, is not a match.
    assert_eq!(
        VideoFormat::from_fourcc(make_fourcc(*b"UYV\0")),
        VideoFormat::None
    );
    assert_eq!(
        VideoFormat::from_fourcc(make_fourcc(*b"\0UYV")),
        VideoFormat::None
    );
}
