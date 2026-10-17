//! Tier 1: safe-API tests mirroring `test/cmocka/test_audio_io_inlines.c`,
//! plus edge cases.
//!
//! The C test's values outside the enums have no safe-API counterpart; the
//! conversions from C values are tested here, and what the C-value helpers
//! in `obs_media_io::ffi::audio_io` return for them in the Tier 3 parity
//! test.

use obs_c_oracle as _;
use obs_media_io::audio_io::{
    AUDIO_OUTPUT_FRAMES, AudioFormat, MAX_AUDIO_MIXES, SpeakerLayout, TOTAL_AUDIO_SIZE,
    audio_bytes_per_channel, audio_channels, audio_format_from_c, audio_format_to_c,
    audio_frames_to_ns, audio_planes, audio_size, is_audio_planar, ns_to_audio_frames,
    speaker_layout_from_c, speaker_layout_to_c, total_audio_size,
};
use proptest as _;

/// As the C test's `formats_outside` and `layouts_outside`, with the last
/// enumerator + 1 written out.
const FORMATS_OUTSIDE: [i32; 3] = [-1, 9, 1000];
const LAYOUTS_OUTSIDE: [i32; 4] = [-1, 7, 9, 1000];

/// As the C test's `format_cases`: every format, in header order, its bytes
/// per channel and whether it is planar.
const FORMAT_CASES: [(AudioFormat, usize, bool); 9] = [
    (AudioFormat::Unknown, 0, false),
    (AudioFormat::U8Bit, 1, false),
    (AudioFormat::Bit16, 2, false),
    (AudioFormat::Bit32, 4, false),
    (AudioFormat::Float, 4, false),
    (AudioFormat::U8BitPlanar, 1, true),
    (AudioFormat::Bit16Planar, 2, true),
    (AudioFormat::Bit32Planar, 4, true),
    (AudioFormat::FloatPlanar, 4, true),
];

/// As the C test's `layout_cases`: every layout, in header order, and its
/// channels.
const LAYOUT_CASES: [(SpeakerLayout, u32); 8] = [
    (SpeakerLayout::Unknown, 0),
    (SpeakerLayout::Mono, 1),
    (SpeakerLayout::Stereo, 2),
    (SpeakerLayout::TwoPointOne, 3),
    (SpeakerLayout::FourPointZero, 4),
    (SpeakerLayout::FourPointOne, 5),
    (SpeakerLayout::FivePointOne, 6),
    (SpeakerLayout::SevenPointOne, 8),
];

const FRAMES: u32 = AUDIO_OUTPUT_FRAMES as u32;

#[test]
fn test_enum_values() {
    for (i, format) in AudioFormat::ALL.into_iter().enumerate() {
        let value = i32::try_from(i).unwrap();
        assert_eq!(audio_format_to_c(format), value, "{format:?}");
        assert_eq!(audio_format_from_c(value), Some(format), "{format:?}");
    }
    // SPEAKERS_7POINT1 is 8: the header skips 7.
    let layouts = [0, 1, 2, 3, 4, 5, 6, 8];
    for (speakers, value) in SpeakerLayout::ALL.into_iter().zip(layouts) {
        assert_eq!(speaker_layout_to_c(speakers), value, "{speakers:?}");
        assert_eq!(speaker_layout_from_c(value), Some(speakers), "{speakers:?}");
    }
}

#[test]
fn test_get_audio_channels() {
    assert_eq!(LAYOUT_CASES.len(), SpeakerLayout::ALL.len());
    for (speakers, channels) in LAYOUT_CASES {
        assert_eq!(audio_channels(speakers), channels, "{speakers:?}");
    }
}

#[test]
fn test_get_audio_channels_outside_enum() {
    // A shim maps these to Unknown, which has no channels.
    for value in LAYOUTS_OUTSIDE {
        assert_eq!(speaker_layout_from_c(value), None, "{value}");
    }
    assert_eq!(audio_channels(SpeakerLayout::Unknown), 0);
}

#[test]
fn test_get_audio_bytes_per_channel() {
    assert_eq!(FORMAT_CASES.len(), AudioFormat::ALL.len());
    for (format, bytes, _) in FORMAT_CASES {
        assert_eq!(audio_bytes_per_channel(format), bytes, "{format:?}");
    }
}

#[test]
fn test_is_audio_planar() {
    for (format, _, planar) in FORMAT_CASES {
        assert_eq!(is_audio_planar(format), planar, "{format:?}");
    }
}

#[test]
fn test_format_helpers_outside_enum() {
    // A shim maps these to Unknown: no bytes, and interleaved.
    for value in FORMATS_OUTSIDE {
        assert_eq!(audio_format_from_c(value), None, "{value}");
    }
    assert_eq!(audio_bytes_per_channel(AudioFormat::Unknown), 0);
    assert!(!is_audio_planar(AudioFormat::Unknown));
}

#[test]
fn test_get_audio_planes() {
    // Planar formats have one plane per channel, interleaved ones a single
    // plane, even with no channels.
    for (format, _, planar) in FORMAT_CASES {
        for (speakers, channels) in LAYOUT_CASES {
            let planes = if planar {
                usize::try_from(channels).unwrap()
            } else {
                1
            };
            assert_eq!(
                audio_planes(format, speakers),
                planes,
                "{format:?} {speakers:?}"
            );
        }
    }
}

#[test]
fn test_get_audio_size() {
    // Bytes per plane: interleaved holds every channel, planar one.
    assert_eq!(audio_size(AudioFormat::Float, SpeakerLayout::Stereo, 1), 8);
    assert_eq!(
        audio_size(AudioFormat::Bit16Planar, SpeakerLayout::FivePointOne, 1),
        2
    );
    assert_eq!(
        audio_size(AudioFormat::Float, SpeakerLayout::Stereo, FRAMES),
        8192
    );
    assert_eq!(
        audio_size(
            AudioFormat::FloatPlanar,
            SpeakerLayout::SevenPointOne,
            FRAMES
        ),
        4096
    );
    assert_eq!(
        audio_size(AudioFormat::U8Bit, SpeakerLayout::Mono, 480),
        480
    );
    assert_eq!(
        audio_size(AudioFormat::Bit32, SpeakerLayout::SevenPointOne, u32::MAX),
        32usize.wrapping_mul(u32::MAX as usize)
    );

    // Zero frames, an unknown format or (interleaved) an unknown layout.
    assert_eq!(audio_size(AudioFormat::Float, SpeakerLayout::Stereo, 0), 0);
    assert_eq!(
        audio_size(AudioFormat::Unknown, SpeakerLayout::Stereo, 1024),
        0
    );
    assert_eq!(
        audio_size(AudioFormat::Float, SpeakerLayout::Unknown, 1024),
        0
    );
    // Planar ignores the layout, even an unknown one.
    assert_eq!(
        audio_size(AudioFormat::FloatPlanar, SpeakerLayout::Unknown, 1024),
        4096
    );
}

#[test]
fn test_get_total_audio_size() {
    // Bytes across all planes, planar or not.
    assert_eq!(
        total_audio_size(AudioFormat::Float, SpeakerLayout::Stereo, 1),
        8
    );
    assert_eq!(
        total_audio_size(AudioFormat::Bit16Planar, SpeakerLayout::FivePointOne, 1),
        12
    );
    assert_eq!(
        total_audio_size(
            AudioFormat::FloatPlanar,
            SpeakerLayout::SevenPointOne,
            FRAMES
        ),
        32768
    );
    assert_eq!(
        total_audio_size(
            AudioFormat::Bit32Planar,
            SpeakerLayout::SevenPointOne,
            u32::MAX
        ),
        32usize.wrapping_mul(u32::MAX as usize)
    );

    assert_eq!(
        total_audio_size(AudioFormat::Float, SpeakerLayout::Stereo, 0),
        0
    );
    assert_eq!(
        total_audio_size(AudioFormat::Unknown, SpeakerLayout::Stereo, 1024),
        0
    );
    assert_eq!(
        total_audio_size(AudioFormat::FloatPlanar, SpeakerLayout::Unknown, 1024),
        0
    );

    // TOTAL_AUDIO_SIZE is every mix at the largest layout, in float.
    assert_eq!(
        TOTAL_AUDIO_SIZE,
        MAX_AUDIO_MIXES
            * total_audio_size(AudioFormat::Float, SpeakerLayout::SevenPointOne, FRAMES)
    );
}

#[test]
fn test_audio_frames_to_ns() {
    assert_eq!(audio_frames_to_ns(48000, 48000), 1_000_000_000);
    assert_eq!(audio_frames_to_ns(44100, 44100), 1_000_000_000);
    assert_eq!(audio_frames_to_ns(48000, 0), 0);
    // Truncated: 1024 frames is 21333333.3 ns at 48 kHz, 23219954.6 at
    // 44.1 kHz.
    assert_eq!(audio_frames_to_ns(48000, 1024), 21_333_333);
    assert_eq!(audio_frames_to_ns(44100, 1024), 23_219_954);
    assert_eq!(audio_frames_to_ns(1, 1), 1_000_000_000);

    // frames * 1e9 is past 64 bits, the result is not, and both
    // mul_div64 paths agree.
    assert_eq!(
        audio_frames_to_ns(48000, 885_443_715_504_000),
        18_446_744_073_000_000_000
    );
}

#[test]
#[should_panic(expected = "zero")]
fn test_audio_frames_to_ns_zero_rate_panics() {
    // Division by zero: UB in C. The message also tells this panic from
    // a todo!().
    audio_frames_to_ns(0, 1);
}

#[test]
fn test_ns_to_audio_frames() {
    assert_eq!(ns_to_audio_frames(48000, 1_000_000_000), 48000);
    assert_eq!(ns_to_audio_frames(44100, 1_000_000_000), 44100);
    assert_eq!(ns_to_audio_frames(48000, 0), 0);
    // Truncated, so a round trip can lose a frame.
    assert_eq!(ns_to_audio_frames(48000, 21_333_333), 1023);
    assert_eq!(audio_frames_to_ns(44100, 1), 22675);
    assert_eq!(ns_to_audio_frames(44100, audio_frames_to_ns(44100, 1)), 0);
    assert_eq!(ns_to_audio_frames(48000, 20_833_333), 999);
    // The rate is the multiplier, so 0 is allowed: no frames.
    assert_eq!(ns_to_audio_frames(0, 1_000_000_000), 0);

    // ns * 48000 is past 64 bits, the result is not, and both mul_div64
    // paths agree.
    assert_eq!(ns_to_audio_frames(48000, u64::MAX), 885_443_715_538_058);
}

#[test]
fn test_frames_ns_round_trip_at_common_rates() {
    // Whole seconds convert exactly both ways.
    for rate in [
        8000, 11025, 16000, 22050, 32000, 44100, 48000, 96000, 192000,
    ] {
        let frames = u64::try_from(rate).unwrap() * 3600;
        let ns = audio_frames_to_ns(rate, frames);
        assert_eq!(ns, 3600 * 1_000_000_000, "{rate}");
        assert_eq!(ns_to_audio_frames(rate, ns), frames, "{rate}");
    }
}
