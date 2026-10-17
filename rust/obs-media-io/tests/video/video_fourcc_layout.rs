//! Tier 2: `enum video_format` crosses the C ABI as a `c_int`, which
//! matches the size and alignment the C compiler gives the enum in
//! `libobs/media-io/video-io.h`, and every enumerator converts to the value
//! the C compiler assigns it.

use core::ffi::c_int;
use core::mem::{align_of, size_of};

use obs_c_oracle::video_fourcc as c;
use obs_media_io::video_io::{VideoFormat, video_format_to_c};
use proptest as _;

#[test]
fn video_format_layout_matches_c_header() {
    // SAFETY: the oracle layout functions take no arguments and only return constants.
    unsafe {
        assert_eq!(size_of::<c_int>(), c::oracle_video_format_size());
        assert_eq!(align_of::<c_int>(), c::oracle_video_format_align());
    }
}

#[test]
fn video_format_values_match_c_header() {
    // SAFETY: as above; `i` stays below the oracle's count.
    unsafe {
        assert_eq!(VideoFormat::ALL.len(), c::oracle_video_format_count());
        for (i, format) in VideoFormat::ALL.into_iter().enumerate() {
            assert_eq!(
                video_format_to_c(format),
                c::oracle_video_format_value(i),
                "{format:?}"
            );
        }
    }
}
