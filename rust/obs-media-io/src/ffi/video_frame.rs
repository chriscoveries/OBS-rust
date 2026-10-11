//! C ABI shims for `libobs/media-io/video-frame.h`.
//!
//! Only `video_frame_init` and `video_frame_copy` are exported.
//! `video_frame_get_linesizes` and `video_frame_get_plane_heights` have no
//! header declaration and no caller outside `video-frame.c`, so they stay in
//! the safe core with no C symbol. The `static inline` helpers
//! (`video_frame_free`, `video_frame_create`, `video_frame_destroy`) stay
//! compiled into every C caller; their equivalents here are plain Rust
//! functions for later Rust ports that handle a `struct video_frame`.
//!
//! The format crosses the C ABI as a `c_int`. Values outside
//! `enum video_format` take the C switches' `default:`, which is
//! `VIDEO_FORMAT_NONE`.

use core::ffi::c_int;
use core::mem::size_of;
use core::ptr;
use core::slice;

use obs_util::ffi::bmem::{bfree, bmalloc};

use crate::video_frame::{FrameLayout, copy_extent, copy_plane, plane_heights};
use crate::video_io::{MAX_AV_PLANES, VideoFormat, video_format_from_c};

/// `struct video_frame`.
#[repr(C)]
#[allow(non_camel_case_types)]
#[derive(Debug, Clone, Copy)]
pub struct video_frame {
    pub data: [*mut u8; MAX_AV_PLANES],
    pub linesize: [u32; MAX_AV_PLANES],
}

fn format_from_c(format: c_int) -> VideoFormat {
    video_format_from_c(format).unwrap_or(VideoFormat::None)
}

/// Allocates one `bmalloc` block for every plane of a `width` by `height`
/// `format` frame and points `frame` at it. A null `frame` is ignored.
/// Crashes in `bmalloc(0)`, as the C does, when no plane has both a linesize
/// and a height (`VIDEO_FORMAT_NONE`, an unknown format, or a zero width or
/// height).
///
/// # Safety
///
/// `frame` must be null or valid for writes of a `struct video_frame`.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn video_frame_init(
    frame: *mut video_frame,
    format: c_int,
    width: u32,
    height: u32,
) {
    if frame.is_null() {
        return;
    }
    let layout = FrameLayout::new(format_from_c(format), width, height);
    // SAFETY: plain allocation; a size of 0 crashes, as in C.
    let data = unsafe { bmalloc(layout.size) }.cast::<u8>();
    let out = video_frame {
        data: layout.offset.map(|offset| match offset {
            Some(offset) => data.wrapping_add(offset),
            None => ptr::null_mut(),
        }),
        linesize: layout.linesize,
    };
    // SAFETY: the caller guarantees `frame` is writable.
    unsafe { ptr::write(frame, out) };
}

/// Copies the first `cy` rows of every plane of `format` from `src` to
/// `dst`, row by row where their linesizes differ.
///
/// # Safety
///
/// `dst` and `src` must point to valid frames. For each plane `format` has,
/// the bytes [`copy_extent`] gives for the two linesizes and that plane's
/// rows must be writable in `dst`, readable in `src`, and not overlap.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn video_frame_copy(
    dst: *mut video_frame,
    src: *const video_frame,
    format: c_int,
    cy: u32,
) {
    // SAFETY: the caller guarantees both frames are valid.
    let (dst, src) = unsafe { (&*dst, &*src) };
    for (i, rows) in plane_heights(format_from_c(format), cy)
        .into_iter()
        .enumerate()
    {
        if rows == 0 {
            continue;
        }
        let (dst_linesize, src_linesize) = (dst.linesize[i], src.linesize[i]);
        let (dst_len, src_len) = copy_extent(dst_linesize, src_linesize, rows);
        if dst_len == 0 {
            continue;
        }
        // SAFETY: the caller guarantees these extents of the planes are
        // valid and do not overlap.
        let (dst_plane, src_plane) = unsafe {
            (
                slice::from_raw_parts_mut(dst.data[i], dst_len),
                slice::from_raw_parts(src.data[i], src_len),
            )
        };
        copy_plane(dst_plane, dst_linesize, src_plane, src_linesize, rows);
    }
}

/// `video_frame_free`: frees `frame`'s planes and zeroes it. Null is
/// ignored.
///
/// # Safety
///
/// `frame` must be null or point to a frame whose `data[0]` is null or a
/// live `bmalloc` block.
pub unsafe fn video_frame_free(frame: *mut video_frame) {
    // SAFETY: the caller guarantees `frame` is null or valid.
    if let Some(frame) = unsafe { frame.as_mut() } {
        // SAFETY: the caller guarantees `data[0]`.
        unsafe { bfree(frame.data[0].cast()) };
        *frame = video_frame {
            data: [ptr::null_mut(); MAX_AV_PLANES],
            linesize: [0; MAX_AV_PLANES],
        };
    }
}

/// `video_frame_create`: a `bzalloc`ed frame, initialized by
/// [`video_frame_init`].
///
/// # Safety
///
/// Crashes like [`video_frame_init`]. Release the frame with
/// [`video_frame_destroy`].
pub unsafe fn video_frame_create(format: c_int, width: u32, height: u32) -> *mut video_frame {
    // SAFETY: plain allocation of a non-zero size.
    let frame = unsafe { bmalloc(size_of::<video_frame>()) }.cast::<video_frame>();
    // SAFETY: `frame` is a fresh block of the right size (`bzalloc`).
    unsafe { ptr::write_bytes(frame, 0, 1) };
    // SAFETY: `frame` is writable.
    unsafe { video_frame_init(frame, format, width, height) };
    frame
}

/// `video_frame_destroy`: frees `frame`'s planes and `frame`. Null is
/// ignored.
///
/// # Safety
///
/// `frame` must be null or come from [`video_frame_create`] (or the C's).
pub unsafe fn video_frame_destroy(frame: *mut video_frame) {
    if !frame.is_null() {
        // SAFETY: the caller guarantees both blocks came from `bmalloc`.
        unsafe {
            bfree((*frame).data[0].cast());
            bfree(frame.cast());
        }
    }
}
