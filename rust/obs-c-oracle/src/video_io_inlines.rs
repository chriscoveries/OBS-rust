//! The `static inline` helpers in `media-io/video-io.h`, as non-inline
//! wrappers (`oracle/video_io_inlines.c`), and the layouts of the structs
//! the header defines and of the enums the `video_fourcc` and
//! `video_matrices` oracles do not cover. The enums are passed as the `int`s they are.

use core::ffi::{c_char, c_int};

unsafe extern "C" {
    pub fn oracle_format_is_yuv(format: c_int) -> bool;
    pub fn oracle_get_video_format_name(format: c_int) -> *const c_char;
    pub fn oracle_get_video_colorspace_name(cs: c_int) -> *const c_char;
    pub fn oracle_resolve_video_range(format: c_int, range: c_int) -> c_int;
    pub fn oracle_get_video_range_name(format: c_int, range: c_int) -> *const c_char;

    pub fn oracle_video_trc_size() -> usize;
    pub fn oracle_video_trc_align() -> usize;
    pub fn oracle_video_trc_count() -> usize;
    pub fn oracle_video_trc_value(i: usize) -> c_int;

    pub fn oracle_video_scale_type_size() -> usize;
    pub fn oracle_video_scale_type_align() -> usize;
    pub fn oracle_video_scale_type_count() -> usize;
    pub fn oracle_video_scale_type_value(i: usize) -> c_int;

    pub fn oracle_video_data_size() -> usize;
    pub fn oracle_video_data_align() -> usize;
    pub fn oracle_video_data_planes() -> usize;
    pub fn oracle_video_data_offset_data() -> usize;
    pub fn oracle_video_data_offset_linesize() -> usize;
    pub fn oracle_video_data_offset_timestamp() -> usize;

    pub fn oracle_video_output_info_size() -> usize;
    pub fn oracle_video_output_info_align() -> usize;
    pub fn oracle_video_output_info_offset_name() -> usize;
    pub fn oracle_video_output_info_offset_format() -> usize;
    pub fn oracle_video_output_info_offset_fps_num() -> usize;
    pub fn oracle_video_output_info_offset_fps_den() -> usize;
    pub fn oracle_video_output_info_offset_width() -> usize;
    pub fn oracle_video_output_info_offset_height() -> usize;
    pub fn oracle_video_output_info_offset_cache_size() -> usize;
    pub fn oracle_video_output_info_offset_colorspace() -> usize;
    pub fn oracle_video_output_info_offset_range() -> usize;

    pub fn oracle_video_scale_info_size() -> usize;
    pub fn oracle_video_scale_info_align() -> usize;
    pub fn oracle_video_scale_info_offset_format() -> usize;
    pub fn oracle_video_scale_info_offset_width() -> usize;
    pub fn oracle_video_scale_info_offset_height() -> usize;
    pub fn oracle_video_scale_info_offset_range() -> usize;
    pub fn oracle_video_scale_info_offset_colorspace() -> usize;
}
