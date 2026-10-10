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

/// `HALF(size)` from `video-frame.c`: `(size + 1) / 2`, wrapping like the C
/// `uint32_t` arithmetic.
fn half(size: u32) -> u32 {
    size.wrapping_add(1) / 2
}

/// `video_frame_get_linesizes`: writes the per-plane linesize in bytes for
/// `width` pixels into `out`. Planes without a linesize stay 0.
#[must_use]
pub fn get_linesizes(format: VideoFormat, width: u32) -> [u32; MAX_AV_PLANES] {
    let mut out = [0u32; MAX_AV_PLANES];
    match format {
        VideoFormat::Bgr3 => out[0] = width.wrapping_mul(3),
        VideoFormat::Rgba
        | VideoFormat::Bgra
        | VideoFormat::Bgrx
        | VideoFormat::Ayuv
        | VideoFormat::R10l => out[0] = width.wrapping_mul(4),
        VideoFormat::P416 => {
            out[0] = width.wrapping_mul(2);
            out[1] = width.wrapping_mul(4);
        }
        VideoFormat::I420 | VideoFormat::I422 => {
            out[0] = width;
            out[1] = half(width);
            out[2] = half(width);
        }
        VideoFormat::I210 | VideoFormat::I010 => {
            out[0] = width.wrapping_mul(2);
            out[1] = width;
            out[2] = width;
        }
        VideoFormat::I40a | VideoFormat::I42a => {
            out[0] = width;
            out[1] = half(width);
            out[2] = half(width);
            out[3] = width;
        }
        VideoFormat::Yvyu | VideoFormat::Yuy2 | VideoFormat::Uyvy => {
            out[0] = width.wrapping_mul(2);
        }
        VideoFormat::P010 | VideoFormat::P216 => {
            out[0] = width.wrapping_mul(2);
            out[1] = width.wrapping_mul(2);
        }
        VideoFormat::I412 => {
            out[0] = width.wrapping_mul(2);
            out[1] = width.wrapping_mul(2);
            out[2] = width.wrapping_mul(2);
        }
        VideoFormat::Ya2l => {
            out[0] = width.wrapping_mul(2);
            out[1] = width.wrapping_mul(2);
            out[2] = width.wrapping_mul(2);
            out[3] = width.wrapping_mul(2);
        }
        VideoFormat::Y800 => out[0] = width,
        VideoFormat::Nv12 => {
            out[0] = width;
            out[1] = width;
        }
        VideoFormat::I444 => {
            out[0] = width;
            out[1] = width;
            out[2] = width;
        }
        VideoFormat::Yuva => {
            out[0] = width;
            out[1] = width;
            out[2] = width;
            out[3] = width;
        }
        VideoFormat::V210 => {
            // Little Endian Compressed: width aligned to 48, then
            // ((width + 5) / 6) * 16 bytes per line.
            let w = width.wrapping_add(47) & !47;
            out[0] = (w.wrapping_add(5) / 6).wrapping_mul(16);
        }
        VideoFormat::None => {}
    }
    out
}

/// `video_frame_get_plane_heights`: writes the per-plane row count for
/// `height` lines into `out`. Planes without rows stay 0.
#[must_use]
pub fn get_plane_heights(format: VideoFormat, height: u32) -> [u32; MAX_AV_PLANES] {
    let mut out = [0u32; MAX_AV_PLANES];
    match format {
        VideoFormat::I420 | VideoFormat::I010 => {
            out[0] = height;
            out[1] = half(height);
            out[2] = half(height);
        }
        VideoFormat::Nv12 | VideoFormat::P010 => {
            out[0] = height;
            out[1] = half(height);
        }
        VideoFormat::Y800
        | VideoFormat::Yvyu
        | VideoFormat::Yuy2
        | VideoFormat::Uyvy
        | VideoFormat::Rgba
        | VideoFormat::Bgra
        | VideoFormat::Bgrx
        | VideoFormat::Bgr3
        | VideoFormat::Ayuv
        | VideoFormat::V210
        | VideoFormat::R10l => out[0] = height,
        VideoFormat::I444 | VideoFormat::I422 | VideoFormat::I210 | VideoFormat::I412 => {
            out[0] = height;
            out[1] = height;
            out[2] = height;
        }
        VideoFormat::I40a => {
            out[0] = height;
            out[1] = half(height);
            out[2] = half(height);
            out[3] = height;
        }
        VideoFormat::I42a | VideoFormat::Yuva | VideoFormat::Ya2l => {
            out[0] = height;
            out[1] = height;
            out[2] = height;
            out[3] = height;
        }
        VideoFormat::P216 | VideoFormat::P416 => {
            out[0] = height;
            out[1] = height;
        }
        VideoFormat::None => {}
    }
    out
}

/// Computes the layout `video_frame_init` would produce for `format`,
/// `width`, `height` with `alignment` bytes of per-plane alignment. When
/// every plane is empty `total` is 0, which is what makes the C code call
/// `bmalloc(0)`.
#[must_use]
pub fn frame_layout(format: VideoFormat, width: u32, height: u32, alignment: usize) -> FrameLayout {
    let mut layout = FrameLayout {
        linesizes: get_linesizes(format, width),
        heights: get_plane_heights(format, height),
        offsets: [0; MAX_AV_PLANES],
        total: 0,
    };

    for i in 0..MAX_AV_PLANES {
        if layout.linesizes[i] == 0 || layout.heights[i] == 0 {
            continue;
        }
        let mut plane_size =
            (layout.linesizes[i] as usize).wrapping_mul(layout.heights[i] as usize);
        plane_size =
            plane_size.wrapping_add(alignment.wrapping_sub(1)) & !(alignment.wrapping_sub(1));
        layout.total = layout.total.wrapping_add(plane_size);
        layout.offsets[i] = layout.total;
    }
    layout
}
