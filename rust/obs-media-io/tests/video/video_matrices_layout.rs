//! Tier 2: `enum video_colorspace` and `enum video_range_type` cross the C
//! ABI as `c_int`s, which match the size and alignment the C compiler gives
//! the enums in `libobs/media-io/video-io.h`, and every enumerator converts
//! to the value the C compiler assigns it. `enum video_format` is checked in
//! `video_fourcc_layout.rs`.

use core::ffi::c_int;
use core::mem::{align_of, size_of};

use obs_c_oracle::video_matrices as c;
use obs_media_io::video_io::{
    VideoColorspace, VideoRangeType, video_colorspace_to_c, video_range_type_to_c,
};
use proptest as _;

#[test]
fn video_colorspace_layout_matches_c_header() {
    // SAFETY: the oracle layout functions take no arguments and only return constants.
    unsafe {
        assert_eq!(size_of::<c_int>(), c::oracle_video_colorspace_size());
        assert_eq!(align_of::<c_int>(), c::oracle_video_colorspace_align());
    }
}

#[test]
fn video_colorspace_values_match_c_header() {
    // SAFETY: as above; `i` stays below the oracle's count.
    unsafe {
        assert_eq!(
            VideoColorspace::ALL.len(),
            c::oracle_video_colorspace_count()
        );
        for (i, cs) in VideoColorspace::ALL.into_iter().enumerate() {
            assert_eq!(
                video_colorspace_to_c(cs),
                c::oracle_video_colorspace_value(i),
                "{cs:?}"
            );
        }
    }
}

#[test]
fn video_range_type_layout_matches_c_header() {
    // SAFETY: the oracle layout functions take no arguments and only return constants.
    unsafe {
        assert_eq!(size_of::<c_int>(), c::oracle_video_range_type_size());
        assert_eq!(align_of::<c_int>(), c::oracle_video_range_type_align());
    }
}

#[test]
fn video_range_type_values_match_c_header() {
    // SAFETY: as above; `i` stays below the oracle's count.
    unsafe {
        assert_eq!(
            VideoRangeType::ALL.len(),
            c::oracle_video_range_type_count()
        );
        for (i, range) in VideoRangeType::ALL.into_iter().enumerate() {
            assert_eq!(
                video_range_type_to_c(range),
                c::oracle_video_range_type_value(i),
                "{range:?}"
            );
        }
    }
}
