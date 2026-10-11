//! Types from `libobs/media-io/video-io.h`.

use core::ffi::c_int;

/// `enum video_format`, in header order. The discriminants are the
/// header's values; the C ABI passes the enum as a `c_int`, converted with
/// [`video_format_to_c`].
#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash)]
pub enum VideoFormat {
    None = 0,

    // planar 4:2:0 formats
    I420 = 1,
    Nv12 = 2,

    // packed 4:2:2 formats
    Yvyu = 3,
    Yuy2 = 4,
    Uyvy = 5,

    // packed uncompressed formats
    Rgba = 6,
    Bgra = 7,
    Bgrx = 8,
    Y800 = 9,

    // planar 4:4:4
    I444 = 10,

    // more packed uncompressed formats
    Bgr3 = 11,

    // planar 4:2:2
    I422 = 12,

    // planar 4:2:0 with alpha
    I40a = 13,

    // planar 4:2:2 with alpha
    I42a = 14,

    // planar 4:4:4 with alpha
    Yuva = 15,

    // packed 4:4:4 with alpha
    Ayuv = 16,

    // planar 4:2:0 format, 10 bpp
    I010 = 17,
    P010 = 18,

    // planar 4:2:2 format, 10 bpp
    I210 = 19,

    // planar 4:4:4 format, 12 bpp
    I412 = 20,

    // planar 4:4:4:4 format, 12 bpp
    Ya2l = 21,

    // planar 4:2:2 format, 16 bpp
    P216 = 22,

    // planar 4:4:4 format, 16 bpp
    P416 = 23,

    // packed 4:2:2 format, 10 bpp
    V210 = 24,

    // packed uncompressed 10-bit format
    R10l = 25,
}

impl VideoFormat {
    /// Every variant, in header order.
    pub const ALL: [Self; 26] = [
        Self::None,
        Self::I420,
        Self::Nv12,
        Self::Yvyu,
        Self::Yuy2,
        Self::Uyvy,
        Self::Rgba,
        Self::Bgra,
        Self::Bgrx,
        Self::Y800,
        Self::I444,
        Self::Bgr3,
        Self::I422,
        Self::I40a,
        Self::I42a,
        Self::Yuva,
        Self::Ayuv,
        Self::I010,
        Self::P010,
        Self::I210,
        Self::I412,
        Self::Ya2l,
        Self::P216,
        Self::P416,
        Self::V210,
        Self::R10l,
    ];
}

/// The C value of `enum video_format` for `format`.
pub fn video_format_to_c(format: VideoFormat) -> c_int {
    format as c_int
}

/// The format whose C value is `value`, or `None` for a value outside
/// `enum video_format`.
pub fn video_format_from_c(value: c_int) -> Option<VideoFormat> {
    VideoFormat::ALL
        .into_iter()
        .find(|&format| video_format_to_c(format) == value)
}

/// `enum video_trc` (transfer characteristics), in header order. The
/// discriminants are the header's values; the C ABI passes the enum as a
/// `c_int`.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash)]
pub enum VideoTrc {
    Default = 0,
    Srgb = 1,
    Pq = 2,
    Hlg = 3,
}

impl VideoTrc {
    /// Every variant, in header order.
    pub const ALL: [Self; 4] = [Self::Default, Self::Srgb, Self::Pq, Self::Hlg];
}

/// The C value of `enum video_trc` for `trc`.
pub fn video_trc_to_c(trc: VideoTrc) -> c_int {
    trc as c_int
}

/// The transfer characteristics whose C value is `value`, or `None` for a
/// value outside `enum video_trc`.
pub fn video_trc_from_c(value: c_int) -> Option<VideoTrc> {
    VideoTrc::ALL
        .into_iter()
        .find(|&trc| video_trc_to_c(trc) == value)
}

/// `enum video_colorspace`, in header order. The discriminants are the
/// header's values; the C ABI passes the enum as a `c_int`.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash)]
pub enum VideoColorspace {
    Default = 0,
    Cs601 = 1,
    Cs709 = 2,
    Srgb = 3,
    Cs2100Pq = 4,
    Cs2100Hlg = 5,
}

impl VideoColorspace {
    /// Every variant, in header order.
    pub const ALL: [Self; 6] = [
        Self::Default,
        Self::Cs601,
        Self::Cs709,
        Self::Srgb,
        Self::Cs2100Pq,
        Self::Cs2100Hlg,
    ];
}

/// The C value of `enum video_colorspace` for `color_space`.
pub fn video_colorspace_to_c(color_space: VideoColorspace) -> c_int {
    color_space as c_int
}

/// The colorspace whose C value is `value`, or `None` for a value outside
/// `enum video_colorspace`.
pub fn video_colorspace_from_c(value: c_int) -> Option<VideoColorspace> {
    VideoColorspace::ALL
        .into_iter()
        .find(|&color_space| video_colorspace_to_c(color_space) == value)
}

/// `enum video_range_type`, in header order. The discriminants are the
/// header's values; the C ABI passes the enum as a `c_int`.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash)]
pub enum VideoRangeType {
    Default = 0,
    Partial = 1,
    Full = 2,
}

impl VideoRangeType {
    /// Every variant, in header order.
    pub const ALL: [Self; 3] = [Self::Default, Self::Partial, Self::Full];
}

/// The C value of `enum video_range_type` for `range`.
pub fn video_range_type_to_c(range: VideoRangeType) -> c_int {
    range as c_int
}

/// The range type whose C value is `value`, or `None` for a value outside
/// `enum video_range_type`.
pub fn video_range_type_from_c(value: c_int) -> Option<VideoRangeType> {
    VideoRangeType::ALL
        .into_iter()
        .find(|&range| video_range_type_to_c(range) == value)
}

/// `enum video_scale_type`, in header order. The discriminants are the
/// header's values; the C ABI passes the enum as a `c_int`.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash)]
pub enum VideoScaleType {
    Default = 0,
    Point = 1,
    FastBilinear = 2,
    Bilinear = 3,
    Bicubic = 4,
}

impl VideoScaleType {
    /// Every variant, in header order.
    pub const ALL: [Self; 5] = [
        Self::Default,
        Self::Point,
        Self::FastBilinear,
        Self::Bilinear,
        Self::Bicubic,
    ];
}

/// The C value of `enum video_scale_type` for `scale_type`.
pub fn video_scale_type_to_c(scale_type: VideoScaleType) -> c_int {
    scale_type as c_int
}

/// The scale type whose C value is `value`, or `None` for a value outside
/// `enum video_scale_type`.
pub fn video_scale_type_from_c(value: c_int) -> Option<VideoScaleType> {
    VideoScaleType::ALL
        .into_iter()
        .find(|&scale_type| video_scale_type_to_c(scale_type) == value)
}

/// `MAX_AV_PLANES` from `libobs/media-io/media-io-defs.h`.
pub const MAX_AV_PLANES: usize = 8;

/// `format_is_yuv`: whether `format` stores YUV rather than RGB or
/// grayscale samples.
pub fn format_is_yuv(format: VideoFormat) -> bool {
    match format {
        VideoFormat::I420
        | VideoFormat::Nv12
        | VideoFormat::I422
        | VideoFormat::I210
        | VideoFormat::Yvyu
        | VideoFormat::Yuy2
        | VideoFormat::Uyvy
        | VideoFormat::I444
        | VideoFormat::I412
        | VideoFormat::I40a
        | VideoFormat::I42a
        | VideoFormat::Yuva
        | VideoFormat::Ya2l
        | VideoFormat::Ayuv
        | VideoFormat::I010
        | VideoFormat::P010
        | VideoFormat::P216
        | VideoFormat::P416
        | VideoFormat::V210 => true,
        VideoFormat::None
        | VideoFormat::Rgba
        | VideoFormat::Bgra
        | VideoFormat::Bgrx
        | VideoFormat::Y800
        | VideoFormat::Bgr3
        | VideoFormat::R10l => false,
    }
}

/// `get_video_format_name`: the short name of `format`, as OBS logs it.
pub fn video_format_name(format: VideoFormat) -> &'static str {
    match format {
        VideoFormat::I420 => "I420",
        VideoFormat::Nv12 => "NV12",
        VideoFormat::I422 => "I422",
        VideoFormat::I210 => "I210",
        VideoFormat::Yvyu => "YVYU",
        VideoFormat::Yuy2 => "YUY2",
        VideoFormat::Uyvy => "UYVY",
        VideoFormat::Rgba => "RGBA",
        VideoFormat::Bgra => "BGRA",
        VideoFormat::Bgrx => "BGRX",
        VideoFormat::I444 => "I444",
        VideoFormat::I412 => "I412",
        VideoFormat::Y800 => "Y800",
        VideoFormat::Bgr3 => "BGR3",
        VideoFormat::I40a => "I40A",
        VideoFormat::I42a => "I42A",
        VideoFormat::Yuva => "YUVA",
        VideoFormat::Ya2l => "YA2L",
        VideoFormat::Ayuv => "AYUV",
        VideoFormat::I010 => "I010",
        VideoFormat::P010 => "P010",
        VideoFormat::P216 => "P216",
        VideoFormat::P416 => "P416",
        VideoFormat::V210 => "v210",
        VideoFormat::R10l => "R10l",
        VideoFormat::None => "None",
    }
}

/// `get_video_colorspace_name`: the display name of `color_space`.
pub fn video_colorspace_name(color_space: VideoColorspace) -> &'static str {
    match color_space {
        VideoColorspace::Default | VideoColorspace::Cs709 => "Rec. 709",
        VideoColorspace::Srgb => "sRGB",
        VideoColorspace::Cs601 => "Rec. 601",
        VideoColorspace::Cs2100Pq => "Rec. 2100 (PQ)",
        VideoColorspace::Cs2100Hlg => "Rec. 2100 (HLG)",
    }
}

/// `resolve_video_range`: `range`, with [`VideoRangeType::Default`]
/// resolved to partial for YUV formats and full otherwise.
pub fn resolve_video_range(format: VideoFormat, range: VideoRangeType) -> VideoRangeType {
    match range {
        VideoRangeType::Default if format_is_yuv(format) => VideoRangeType::Partial,
        VideoRangeType::Default => VideoRangeType::Full,
        range => range,
    }
}

/// `get_video_range_name`: `"Full"` or `"Partial"`, after
/// [`resolve_video_range`].
pub fn video_range_name(format: VideoFormat, range: VideoRangeType) -> &'static str {
    if resolve_video_range(format, range) == VideoRangeType::Full {
        "Full"
    } else {
        "Partial"
    }
}
