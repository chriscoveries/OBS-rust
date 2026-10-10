//! C ABI shims for the exported functions in `libobs/media-io/video-frame.h`.
//!
//! The shims adapt raw C pointers to the safe core in
//! [`crate::video_frame`]. They keep the C allocation behavior: a single
//! `bmalloc` block owned by `data[0]`, freed by the header-inline
//! `video_frame_free`/`video_frame_destroy` with `bfree`.

use core::ffi::{c_int, c_void};
use core::ptr;

use crate::video_frame::{MAX_AV_PLANES, VideoFormat, frame_layout, get_plane_heights};

unsafe extern "C" {
    fn bmalloc(size: usize) -> *mut c_void;
    fn base_get_alignment() -> c_int;
}

/// `struct video_frame` from `libobs/media-io/video-frame.h`.
#[repr(C)]
#[allow(non_camel_case_types)]
#[derive(Debug)]
pub struct video_frame {
    pub data: [*mut u8; MAX_AV_PLANES],
    pub linesize: [u32; MAX_AV_PLANES],
}

/// `video_frame_init`: zeroes `*frame`, computes per-plane linesizes and
/// heights, `bmalloc`s the total and points each used plane into the block.
///
/// # Safety
///
/// `frame` may be null (a no-op) or must be valid for writes of a
/// `video_frame`. Every other contract is the C one: `format` values with no
/// planes produce a `bmalloc(0)` call, which aborts in libobs.
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
    // SAFETY: the caller guarantees `frame` is writable for a whole struct.
    unsafe { ptr::write_bytes(frame, 0, 1) };

    // SAFETY: base_get_alignment has no preconditions.
    let alignment = unsafe { base_get_alignment() } as usize;
    let layout = frame_layout(VideoFormat::from_c(format), width, height, alignment);

    // SAFETY: total is the C-computed size; a 0 total preserves the C
    // bmalloc(0) abort.
    let buf = unsafe { bmalloc(layout.total) } as *mut u8;
    // SAFETY: `frame` was zeroed above; only used planes get pointers,
    // leaving the rest null as C does.
    unsafe {
        (*frame).data[0] = buf;
        (*frame).linesize[0] = layout.linesizes[0];
        for i in 1..MAX_AV_PLANES {
            if layout.linesizes[i] == 0 || layout.heights[i] == 0 {
                continue;
            }
            (*frame).data[i] = buf.add(layout.offsets[i - 1]);
            (*frame).linesize[i] = layout.linesizes[i];
        }
    }
}

/// `video_frame_copy`: copies each plane of `*src` into `*dst` for `cy`
/// lines. Equal linesizes copy `linesize*height` bytes (the product wraps
/// at 32 bits, as in C); mismatched ones copy `min(linesize)` per row.
///
/// # Safety
///
/// `dst` and `src` must be valid `video_frame`s whose data pointers are
/// valid for the byte counts the C contract implies.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn video_frame_copy(
    dst: *mut video_frame,
    src: *const video_frame,
    format: c_int,
    cy: u32,
) {
    let heights = get_plane_heights(VideoFormat::from_c(format), cy);
    // SAFETY: the caller guarantees `dst` and `src` are valid frames and
    // each used plane's buffer covers linesize*height bytes.
    unsafe {
        let (d, s) = (&mut *dst, &*src);
        for i in 0..MAX_AV_PLANES {
            if heights[i] == 0 {
                continue;
            }
            let sp = s.data[i];
            let dp = d.data[i];
            let sls = s.linesize[i];
            let dls = d.linesize[i];
            if sls == dls {
                // C: memcpy(dst, src, src->linesize[i] * heights[i]) in u32.
                ptr::copy_nonoverlapping(sp, dp, sls.wrapping_mul(heights[i]) as usize);
            } else {
                let sls = sls as usize;
                let dls = dls as usize;
                let ls = sls.min(dls);
                for y in 0..heights[i] as usize {
                    ptr::copy_nonoverlapping(sp.add(sls * y), dp.add(dls * y), ls);
                }
            }
        }
    }
}
