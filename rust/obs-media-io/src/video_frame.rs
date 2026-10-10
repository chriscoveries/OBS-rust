//! Safe core of `libobs/media-io/video-frame.c`.
//!
//! Mirrors the C per-plane layout arithmetic exactly, including its integer
//! widths: linesize and height math is `u32` (wrapping, like the C
//! `uint32_t`), while plane sizes and offsets are `usize` (`size_t`).
//! Allocation stays in the C ABI shim, which `bmalloc`s a single block the
//! plane pointers slice into.

/// `MAX_AV_PLANES` from `libobs/media-io/media-io-defs.h`.
pub const MAX_AV_PLANES: usize = 8;

/// `enum video_format` from `libobs/media-io/video-io.h`, same discriminants.
/// Values outside the known range map to [`VideoFormat::None`], matching the
/// C `default:` case of every switch that consumes the enum.
#[derive(Debug, Clone, Copy, Default, PartialEq, Eq)]
#[repr(i32)]
#[allow(non_camel_case_types)]
pub enum VideoFormat {
    #[default]
    None = 0,
    I420 = 1,
    Nv12 = 2,
    Yvyu = 3,
    Yuy2 = 4,
    Uyvy = 5,
    Rgba = 6,
    Bgra = 7,
    Bgrx = 8,
    Y800 = 9,
    I444 = 10,
    Bgr3 = 11,
    I422 = 12,
    I40a = 13,
    I42a = 14,
    Yuva = 15,
    Ayuv = 16,
    I010 = 17,
    P010 = 18,
    I210 = 19,
    I412 = 20,
    Ya2l = 21,
    P216 = 22,
    P416 = 23,
    V210 = 24,
    R10l = 25,
}

impl VideoFormat {
    /// Converts a C `enum video_format` value. Unknown values behave like
    /// `VIDEO_FORMAT_NONE` in the C code (every switch has `default:` next to
    /// it), so they map to [`VideoFormat::None`].
    #[must_use]
    pub fn from_c(value: i32) -> Self {
        match value {
            1 => Self::I420,
            2 => Self::Nv12,
            3 => Self::Yvyu,
            4 => Self::Yuy2,
            5 => Self::Uyvy,
            6 => Self::Rgba,
            7 => Self::Bgra,
            8 => Self::Bgrx,
            9 => Self::Y800,
            10 => Self::I444,
            11 => Self::Bgr3,
            12 => Self::I422,
            13 => Self::I40a,
            14 => Self::I42a,
            15 => Self::Yuva,
            16 => Self::Ayuv,
            17 => Self::I010,
            18 => Self::P010,
            19 => Self::I210,
            20 => Self::I412,
            21 => Self::Ya2l,
            22 => Self::P216,
            23 => Self::P416,
            24 => Self::V210,
            25 => Self::R10l,
            _ => Self::None,
        }
    }
}

/// The aligned layout `video_frame_init` computes: per-plane linesizes and
/// heights, the cumulative offset at which each plane's buffer starts, and
/// the total size of the single `bmalloc` block.
#[derive(Debug, Clone, Default, PartialEq, Eq)]
pub struct FrameLayout {
    pub linesizes: [u32; MAX_AV_PLANES],
    pub heights: [u32; MAX_AV_PLANES],
    pub offsets: [usize; MAX_AV_PLANES],
    pub total: usize,
}

/// `video_frame_get_linesizes`: writes the per-plane linesize in bytes for
/// `width` pixels into `out`. Planes without a linesize stay 0.
#[must_use]
pub fn get_linesizes(format: VideoFormat, width: u32) -> [u32; MAX_AV_PLANES] {
    todo!()
}

/// `video_frame_get_plane_heights`: writes the per-plane row count for
/// `height` lines into `out`. Planes without rows stay 0.
#[must_use]
pub fn get_plane_heights(format: VideoFormat, height: u32) -> [u32; MAX_AV_PLANES] {
    todo!()
}

/// Computes the layout `video_frame_init` would produce for `format`,
/// `width`, `height` with `alignment` bytes of per-plane alignment. When
/// every plane is empty `total` is 0, which is what makes the C code call
/// `bmalloc(0)`.
#[must_use]
pub fn frame_layout(format: VideoFormat, width: u32, height: u32, alignment: usize) -> FrameLayout {
    todo!()
}
