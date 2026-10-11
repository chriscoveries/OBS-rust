//! Port of `libobs/media-io/video-frame.c` and the `static inline` helpers
//! in `video-frame.h`.
//!
//! A frame is one allocation holding every plane, each padded to the bmem
//! alignment. [`FrameLayout`] is where `video_frame_init` puts the planes,
//! and [`VideoFrame`] owns a buffer laid out that way. The arithmetic is the
//! C's: `u32` linesizes wrap, and the plane sizes and offsets are `usize`.

use obs_util::bmem::ALIGNMENT;

use crate::video_io::{MAX_AV_PLANES, VideoFormat};

/// `HALF`: half of `size`, rounded up, in `u32`, so `half(u32::MAX)` wraps
/// to 0.
fn half(size: u32) -> u32 {
    size.wrapping_add(1) / 2
}

/// `align_size`: `size` rounded up to the bmem alignment, wrapping.
fn align_size(size: usize) -> usize {
    size.wrapping_add(ALIGNMENT - 1) & !(ALIGNMENT - 1)
}

/// `video_frame_get_linesizes`: the bytes per row of each plane of a
/// `width`-pixel frame, 0 for the planes `format` does not have.
///
/// Characterized, not endorsed: V210 rounds `width` up with the C's `ALIGN`
/// to 48, but `ALIGN` masks with `!(alignment - 1)`, which only rounds to a
/// power of two. Width 1 becomes 16 and 1280 stays 1280, so the linesize is
/// 3424, not 3456. It is never below `width` unless `width + 47` wraps, so a
/// row still fits.
pub fn linesizes(format: VideoFormat, width: u32) -> [u32; MAX_AV_PLANES] {
    let full = width;
    let double = width.wrapping_mul(2);
    let half = half(width);
    let mut l = [0; MAX_AV_PLANES];
    match format {
        VideoFormat::None => {}
        VideoFormat::Bgr3 => l[0] = width.wrapping_mul(3),
        VideoFormat::Rgba
        | VideoFormat::Bgra
        | VideoFormat::Bgrx
        | VideoFormat::Ayuv
        | VideoFormat::R10l => l[0] = width.wrapping_mul(4),
        VideoFormat::P416 => {
            l[0] = double;
            l[1] = width.wrapping_mul(4);
        }
        VideoFormat::I420 | VideoFormat::I422 => l[..3].copy_from_slice(&[full, half, half]),
        VideoFormat::I210 | VideoFormat::I010 => l[..3].copy_from_slice(&[double, full, full]),
        VideoFormat::I40a | VideoFormat::I42a => {
            l[..4].copy_from_slice(&[full, half, half, full]);
        }
        VideoFormat::Yvyu | VideoFormat::Yuy2 | VideoFormat::Uyvy => l[0] = double,
        VideoFormat::P010 | VideoFormat::P216 => l[..2].fill(double),
        VideoFormat::I412 => l[..3].fill(double),
        VideoFormat::Ya2l => l[..4].fill(double),
        VideoFormat::Y800 => l[0] = full,
        VideoFormat::Nv12 => l[..2].fill(full),
        VideoFormat::I444 => l[..3].fill(full),
        VideoFormat::Yuva => l[..4].fill(full),
        VideoFormat::V210 => {
            let aligned = width.wrapping_add(48 - 1) & !(48 - 1);
            l[0] = (aligned.wrapping_add(5) / 6).wrapping_mul(16);
        }
    }
    l
}

/// `video_frame_get_plane_heights`: the rows of each plane of a
/// `height`-row frame, 0 for the planes `format` does not have.
pub fn plane_heights(format: VideoFormat, height: u32) -> [u32; MAX_AV_PLANES] {
    let full = height;
    let half = half(height);
    let mut h = [0; MAX_AV_PLANES];
    match format {
        VideoFormat::None => {}
        VideoFormat::I420 | VideoFormat::I010 => h[..3].copy_from_slice(&[full, half, half]),
        VideoFormat::Nv12 | VideoFormat::P010 => h[..2].copy_from_slice(&[full, half]),
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
        | VideoFormat::R10l => h[0] = full,
        VideoFormat::I444 | VideoFormat::I422 | VideoFormat::I210 | VideoFormat::I412 => {
            h[..3].fill(full);
        }
        VideoFormat::I40a => h[..4].copy_from_slice(&[full, half, half, full]),
        VideoFormat::I42a | VideoFormat::Yuva | VideoFormat::Ya2l => h[..4].fill(full),
        VideoFormat::P216 | VideoFormat::P416 => h[..2].fill(full),
    }
    h
}

/// Where `video_frame_init` puts each plane of a frame.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct FrameLayout {
    /// Bytes to allocate for all planes. 0 when no plane has both a
    /// linesize and a height, which `bmalloc` refuses.
    pub size: usize,
    /// The frame's `linesize`: plane 0's always, then each plane that has
    /// both a linesize and a height.
    pub linesize: [u32; MAX_AV_PLANES],
    /// Each plane's byte offset from `data[0]`, `None` where `data[i]` stays
    /// null. Plane 0 is always at 0.
    ///
    /// Characterized, not endorsed: plane `i` starts where plane `i - 1`
    /// ended, or at 0 if plane `i - 1` was skipped. A skip only happens when
    /// a linesize wraps to 0 (e.g. I40A at width `u32::MAX`, where planes 1
    /// and 2 are skipped and plane 3 lands on plane 0).
    pub offset: [Option<usize>; MAX_AV_PLANES],
}

impl FrameLayout {
    /// The layout `video_frame_init` gives a `width` by `height` frame.
    pub fn new(format: VideoFormat, width: u32, height: u32) -> Self {
        let linesizes = linesizes(format, width);
        let heights = plane_heights(format, height);
        let present = |i: usize| linesizes[i] != 0 && heights[i] != 0;

        let mut size = 0usize;
        let mut ends = [0usize; MAX_AV_PLANES];
        for i in (0..MAX_AV_PLANES).filter(|&i| present(i)) {
            let plane_size = (linesizes[i] as usize).wrapping_mul(heights[i] as usize);
            size = size.wrapping_add(align_size(plane_size));
            ends[i] = size;
        }

        let mut linesize = [0; MAX_AV_PLANES];
        let mut offset = [None; MAX_AV_PLANES];
        linesize[0] = linesizes[0];
        offset[0] = Some(0);
        for i in (1..MAX_AV_PLANES).filter(|&i| present(i)) {
            linesize[i] = linesizes[i];
            offset[i] = Some(ends[i - 1]);
        }
        Self {
            size,
            linesize,
            offset,
        }
    }
}

/// The bytes of `(dst, src)` that copying `rows` rows of one plane touches,
/// counted from the plane's start: `linesize * rows`, wrapped to `u32`, when
/// the linesizes match, else up to the end of the last row's shorter
/// linesize. `(0, 0)` when nothing is copied.
pub fn copy_extent(dst_linesize: u32, src_linesize: u32, rows: u32) -> (usize, usize) {
    if dst_linesize == src_linesize {
        let len = src_linesize.wrapping_mul(rows) as usize;
        return (len, len);
    }
    let len = dst_linesize.min(src_linesize) as usize;
    if len == 0 || rows == 0 {
        return (0, 0);
    }
    let last = rows as usize - 1;
    (
        (dst_linesize as usize).wrapping_mul(last).wrapping_add(len),
        (src_linesize as usize).wrapping_mul(last).wrapping_add(len),
    )
}

/// One plane of `video_frame_copy`: copies `rows` rows from `src` to `dst`.
/// Equal linesizes copy `linesize * rows` bytes (wrapped to `u32`) in one
/// go; otherwise each row copies the shorter linesize, leaving the rest of a
/// wider `dst` row untouched.
///
/// # Panics
///
/// If `dst` or `src` is shorter than [`copy_extent`].
pub fn copy_plane(dst: &mut [u8], dst_linesize: u32, src: &[u8], src_linesize: u32, rows: u32) {
    let (dst_len, src_len) = copy_extent(dst_linesize, src_linesize, rows);
    if dst_linesize == src_linesize {
        dst[..dst_len].copy_from_slice(&src[..src_len]);
        return;
    }
    if dst_len == 0 {
        return;
    }
    let (dst, src) = (&mut dst[..dst_len], &src[..src_len]);
    let len = dst_linesize.min(src_linesize) as usize;
    for y in 0..rows as usize {
        let d = dst_linesize as usize * y;
        let s = src_linesize as usize * y;
        dst[d..d + len].copy_from_slice(&src[s..s + len]);
    }
}

/// An owned frame laid out as `video_frame_init` lays it out
/// (`video_frame_create`; dropping it is `video_frame_destroy`).
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct VideoFrame {
    data: Box<[u8]>,
    layout: FrameLayout,
}

impl VideoFrame {
    /// A zeroed `width` by `height` frame.
    ///
    /// # Panics
    ///
    /// If the layout's size is 0 (`VideoFormat::None`, or a zero width or
    /// height), where C's `bmalloc(0)` crashes.
    pub fn new(format: VideoFormat, width: u32, height: u32) -> Self {
        let layout = FrameLayout::new(format, width, height);
        assert!(
            layout.size != 0,
            "bmalloc: Allocating 0 bytes is broken behavior, please fix your code!"
        );
        Self {
            data: vec![0; layout.size].into_boxed_slice(),
            layout,
        }
    }

    pub fn layout(&self) -> &FrameLayout {
        &self.layout
    }

    pub fn linesize(&self, plane: usize) -> u32 {
        self.layout.linesize[plane]
    }

    /// The frame's bytes from `plane`'s start to the end of the buffer, or
    /// `None` for a plane the frame has no pointer for.
    pub fn plane(&self, plane: usize) -> Option<&[u8]> {
        Some(&self.data[self.layout.offset[plane]?..])
    }

    /// As [`Self::plane`], mutably.
    pub fn plane_mut(&mut self, plane: usize) -> Option<&mut [u8]> {
        Some(&mut self.data[self.layout.offset[plane]?..])
    }

    /// `video_frame_copy`: copies the first `cy` rows of every plane of
    /// `format` from `src`, row by row where the linesizes differ.
    ///
    /// # Panics
    ///
    /// If either frame is too small for `cy` rows of `format`, or lacks a
    /// plane with bytes to copy.
    pub fn copy_from(&mut self, src: &VideoFrame, format: VideoFormat, cy: u32) {
        for (i, rows) in plane_heights(format, cy).into_iter().enumerate() {
            if rows == 0 {
                continue;
            }
            let (dst_linesize, src_linesize) = (self.linesize(i), src.linesize(i));
            if copy_extent(dst_linesize, src_linesize, rows) == (0, 0) {
                continue;
            }
            let src_plane = src
                .plane(i)
                .expect("src has no pointer for a plane to copy");
            let dst_plane = self
                .plane_mut(i)
                .expect("dst has no pointer for a plane to copy");
            copy_plane(dst_plane, dst_linesize, src_plane, src_linesize, rows);
        }
    }
}
