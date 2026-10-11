//! The structs of `libobs/media-io/video-io.h` at the C boundary, and its
//! `static inline` helpers for enum values as they cross it.
//!
//! The helpers are compiled into every C caller, so nothing is exported or
//! swapped here. The functions below are for Rust shims that receive the
//! enums as `c_int`s: they convert, call the safe core in
//! [`crate::video_io`], and give a value outside its enum the result the
//! C gives it. A format outside `enum video_format` is not YUV and is named
//! `"None"`, like `VIDEO_FORMAT_NONE`; a colorspace outside
//! `enum video_colorspace` is named `"Unknown"`; a range outside
//! `enum video_range_type` is not `VIDEO_RANGE_DEFAULT`, so
//! `resolve_video_range` returns it unchanged and it is named `"Partial"`.

use core::ffi::{c_char, c_int};

use crate::video_io::{
    self, MAX_AV_PLANES, VideoFormat, video_colorspace_from_c, video_format_from_c,
    video_range_type_from_c, video_range_type_to_c,
};

/// Mirrors `struct video_data`.
#[repr(C)]
#[allow(non_camel_case_types)]
#[derive(Debug, Clone, Copy)]
pub struct video_data {
    pub data: [*mut u8; MAX_AV_PLANES],
    pub linesize: [u32; MAX_AV_PLANES],
    pub timestamp: u64,
}

/// Mirrors `struct video_output_info`. The enums are `c_int`s.
#[repr(C)]
#[allow(non_camel_case_types)]
#[derive(Debug, Clone, Copy)]
pub struct video_output_info {
    pub name: *const c_char,

    pub format: c_int,
    pub fps_num: u32,
    pub fps_den: u32,
    pub width: u32,
    pub height: u32,
    pub cache_size: usize,

    pub colorspace: c_int,
    pub range: c_int,
}

/// Mirrors `struct video_scale_info`. The enums are `c_int`s.
#[repr(C)]
#[allow(non_camel_case_types)]
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct video_scale_info {
    pub format: c_int,
    pub width: u32,
    pub height: u32,
    pub range: c_int,
    pub colorspace: c_int,
}

/// `format_is_yuv` for a C `enum video_format` value.
pub fn format_is_yuv(format: c_int) -> bool {
    video_format_from_c(format).is_some_and(video_io::format_is_yuv)
}

/// `get_video_format_name` for a C `enum video_format` value.
pub fn get_video_format_name(format: c_int) -> &'static str {
    video_io::video_format_name(video_format_from_c(format).unwrap_or(VideoFormat::None))
}

/// `get_video_colorspace_name` for a C `enum video_colorspace` value.
pub fn get_video_colorspace_name(cs: c_int) -> &'static str {
    video_colorspace_from_c(cs).map_or("Unknown", video_io::video_colorspace_name)
}

/// `resolve_video_range` for C `enum video_format` and
/// `enum video_range_type` values.
pub fn resolve_video_range(format: c_int, range: c_int) -> c_int {
    let format = video_format_from_c(format).unwrap_or(VideoFormat::None);
    video_range_type_from_c(range).map_or(range, |range| {
        video_range_type_to_c(video_io::resolve_video_range(format, range))
    })
}

/// `get_video_range_name` for C `enum video_format` and
/// `enum video_range_type` values.
pub fn get_video_range_name(format: c_int, range: c_int) -> &'static str {
    let format = video_format_from_c(format).unwrap_or(VideoFormat::None);
    video_range_type_from_c(range)
        .map_or("Partial", |range| video_io::video_range_name(format, range))
}
