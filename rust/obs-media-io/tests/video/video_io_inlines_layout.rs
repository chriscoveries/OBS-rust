//! Tier 2 (header-inline exemption): the `#[repr(C)]` mirrors of
//! `struct video_data`, `struct video_output_info`, `struct video_scale_info`
//! (`libobs/media-io/video-io.h`) and `struct media_frames_per_second`
//! (`libobs/media-io/frame-rate.h`) match the layouts the C compiler
//! produces. The enum fields are `c_int`s; `video_fourcc_layout.rs` and
//! `video_matrices_layout.rs` check that they match the C enums.
//! `enum video_trc` and `enum video_scale_type` are checked here.

use core::ffi::c_int;
use core::mem::{align_of, offset_of, size_of};

use obs_c_oracle::frame_rate as fr;
use obs_c_oracle::video_io_inlines as c;
use obs_media_io::ffi::frame_rate::media_frames_per_second;
use obs_media_io::ffi::video_io::{video_data, video_output_info, video_scale_info};
use obs_media_io::video_io::{
    MAX_AV_PLANES, VideoScaleType, VideoTrc, video_scale_type_to_c, video_trc_to_c,
};
use proptest as _;

#[test]
fn video_trc_layout_matches_c_header() {
    // SAFETY: the oracle layout functions take no arguments and only return constants.
    unsafe {
        assert_eq!(size_of::<c_int>(), c::oracle_video_trc_size());
        assert_eq!(align_of::<c_int>(), c::oracle_video_trc_align());
    }
}

#[test]
fn video_trc_values_match_c_header() {
    // SAFETY: as above; `i` stays below the oracle's count.
    unsafe {
        assert_eq!(VideoTrc::ALL.len(), c::oracle_video_trc_count());
        for (i, trc) in VideoTrc::ALL.into_iter().enumerate() {
            assert_eq!(video_trc_to_c(trc), c::oracle_video_trc_value(i), "{trc:?}");
        }
    }
}

#[test]
fn video_scale_type_layout_matches_c_header() {
    // SAFETY: the oracle layout functions take no arguments and only return constants.
    unsafe {
        assert_eq!(size_of::<c_int>(), c::oracle_video_scale_type_size());
        assert_eq!(align_of::<c_int>(), c::oracle_video_scale_type_align());
    }
}

#[test]
fn video_scale_type_values_match_c_header() {
    // SAFETY: as above; `i` stays below the oracle's count.
    unsafe {
        assert_eq!(
            VideoScaleType::ALL.len(),
            c::oracle_video_scale_type_count()
        );
        for (i, scale_type) in VideoScaleType::ALL.into_iter().enumerate() {
            assert_eq!(
                video_scale_type_to_c(scale_type),
                c::oracle_video_scale_type_value(i),
                "{scale_type:?}"
            );
        }
    }
}

#[test]
fn video_data_layout_matches_c_header() {
    // SAFETY: the oracle layout functions take no arguments and only return constants.
    unsafe {
        assert_eq!(MAX_AV_PLANES, c::oracle_video_data_planes());
        assert_eq!(size_of::<video_data>(), c::oracle_video_data_size());
        assert_eq!(align_of::<video_data>(), c::oracle_video_data_align());
        assert_eq!(
            offset_of!(video_data, data),
            c::oracle_video_data_offset_data()
        );
        assert_eq!(
            offset_of!(video_data, linesize),
            c::oracle_video_data_offset_linesize()
        );
        assert_eq!(
            offset_of!(video_data, timestamp),
            c::oracle_video_data_offset_timestamp()
        );
    }
}

#[test]
fn video_output_info_layout_matches_c_header() {
    // SAFETY: as above.
    unsafe {
        assert_eq!(
            size_of::<video_output_info>(),
            c::oracle_video_output_info_size()
        );
        assert_eq!(
            align_of::<video_output_info>(),
            c::oracle_video_output_info_align()
        );
        assert_eq!(
            offset_of!(video_output_info, name),
            c::oracle_video_output_info_offset_name()
        );
        assert_eq!(
            offset_of!(video_output_info, format),
            c::oracle_video_output_info_offset_format()
        );
        assert_eq!(
            offset_of!(video_output_info, fps_num),
            c::oracle_video_output_info_offset_fps_num()
        );
        assert_eq!(
            offset_of!(video_output_info, fps_den),
            c::oracle_video_output_info_offset_fps_den()
        );
        assert_eq!(
            offset_of!(video_output_info, width),
            c::oracle_video_output_info_offset_width()
        );
        assert_eq!(
            offset_of!(video_output_info, height),
            c::oracle_video_output_info_offset_height()
        );
        assert_eq!(
            offset_of!(video_output_info, cache_size),
            c::oracle_video_output_info_offset_cache_size()
        );
        assert_eq!(
            offset_of!(video_output_info, colorspace),
            c::oracle_video_output_info_offset_colorspace()
        );
        assert_eq!(
            offset_of!(video_output_info, range),
            c::oracle_video_output_info_offset_range()
        );
    }
}

#[test]
fn video_scale_info_layout_matches_c_header() {
    // SAFETY: as above.
    unsafe {
        assert_eq!(
            size_of::<video_scale_info>(),
            c::oracle_video_scale_info_size()
        );
        assert_eq!(
            align_of::<video_scale_info>(),
            c::oracle_video_scale_info_align()
        );
        assert_eq!(
            offset_of!(video_scale_info, format),
            c::oracle_video_scale_info_offset_format()
        );
        assert_eq!(
            offset_of!(video_scale_info, width),
            c::oracle_video_scale_info_offset_width()
        );
        assert_eq!(
            offset_of!(video_scale_info, height),
            c::oracle_video_scale_info_offset_height()
        );
        assert_eq!(
            offset_of!(video_scale_info, range),
            c::oracle_video_scale_info_offset_range()
        );
        assert_eq!(
            offset_of!(video_scale_info, colorspace),
            c::oracle_video_scale_info_offset_colorspace()
        );
    }
}

#[test]
fn media_frames_per_second_layout_matches_c_header() {
    // SAFETY: as above.
    unsafe {
        assert_eq!(
            size_of::<media_frames_per_second>(),
            fr::oracle_media_frames_per_second_size()
        );
        assert_eq!(
            align_of::<media_frames_per_second>(),
            fr::oracle_media_frames_per_second_align()
        );
        assert_eq!(
            offset_of!(media_frames_per_second, numerator),
            fr::oracle_media_frames_per_second_offset_numerator()
        );
        assert_eq!(
            offset_of!(media_frames_per_second, denominator),
            fr::oracle_media_frames_per_second_offset_denominator()
        );
    }
}
