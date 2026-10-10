//! Tier 1 port of `test/cmocka/test_video_frame.c` for the Rust core and C
//! ABI shims of `video-frame.c`. Same assertions: per-plane linesizes and
//! buffer offsets for every format, null-frame no-op, same/wider/narrower
//! copies.

use obs_c_oracle as _; // links the test bmalloc/bfree (oracle/test_bmem.c)

use core::ffi::c_void;
use core::ptr;

use obs_media_io::ffi::video_frame::{video_frame, video_frame_copy, video_frame_init};
use obs_media_io::video_frame::{MAX_AV_PLANES, VideoFormat, frame_layout};

unsafe extern "C" {
    fn bfree(ptr: *mut c_void);
    fn base_get_alignment() -> i32;
}

const FORMATS: [VideoFormat; 25] = [
    VideoFormat::I420,
    VideoFormat::Nv12,
    VideoFormat::Yvyu,
    VideoFormat::Yuy2,
    VideoFormat::Uyvy,
    VideoFormat::Rgba,
    VideoFormat::Bgra,
    VideoFormat::Bgrx,
    VideoFormat::Y800,
    VideoFormat::I444,
    VideoFormat::Bgr3,
    VideoFormat::I422,
    VideoFormat::I40a,
    VideoFormat::I42a,
    VideoFormat::Yuva,
    VideoFormat::Ayuv,
    VideoFormat::I010,
    VideoFormat::P010,
    VideoFormat::I210,
    VideoFormat::I412,
    VideoFormat::Ya2l,
    VideoFormat::P216,
    VideoFormat::P416,
    VideoFormat::V210,
    VideoFormat::R10l,
];

/// Expected linesize table, ported from the cmocka characterization test.
fn expected_linesizes(format: VideoFormat, width: u32) -> [u32; MAX_AV_PLANES] {
    let mut out = [0u32; MAX_AV_PLANES];
    let half = width.wrapping_add(1) / 2;
    match format {
        VideoFormat::Bgr3 => out[0] = width * 3,
        VideoFormat::Rgba
        | VideoFormat::Bgra
        | VideoFormat::Bgrx
        | VideoFormat::Ayuv
        | VideoFormat::R10l => out[0] = width * 4,
        VideoFormat::P416 => {
            out[0] = width * 2;
            out[1] = width * 4;
        }
        VideoFormat::I420 | VideoFormat::I422 => {
            out[0] = width;
            out[1] = half;
            out[2] = half;
        }
        VideoFormat::I210 | VideoFormat::I010 => {
            out[0] = width * 2;
            out[1] = width;
            out[2] = width;
        }
        VideoFormat::I40a | VideoFormat::I42a => {
            out[0] = width;
            out[1] = half;
            out[2] = half;
            out[3] = width;
        }
        VideoFormat::Yvyu | VideoFormat::Yuy2 | VideoFormat::Uyvy => out[0] = width * 2,
        VideoFormat::P010 | VideoFormat::P216 => {
            out[0] = width * 2;
            out[1] = width * 2;
        }
        VideoFormat::I412 => {
            out[0] = width * 2;
            out[1] = width * 2;
            out[2] = width * 2;
        }
        VideoFormat::Ya2l => {
            out[0] = width * 2;
            out[1] = width * 2;
            out[2] = width * 2;
            out[3] = width * 2;
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
            let w = width.wrapping_add(47) & !47;
            out[0] = w.wrapping_add(5) / 6 * 16;
        }
        VideoFormat::None => {}
    }
    out
}

/// Expected plane-height table, ported from the cmocka characterization test.
fn expected_heights(format: VideoFormat, height: u32) -> [u32; MAX_AV_PLANES] {
    let mut out = [0u32; MAX_AV_PLANES];
    let half = height.wrapping_add(1) / 2;
    match format {
        VideoFormat::I420 | VideoFormat::I010 => {
            out[0] = height;
            out[1] = half;
            out[2] = half;
        }
        VideoFormat::Nv12 | VideoFormat::P010 => {
            out[0] = height;
            out[1] = half;
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
            out[1] = half;
            out[2] = half;
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

/// Frees the frame's backing block like the header-inline
/// `video_frame_free`, then zeroes the struct.
unsafe fn frame_free(frame: &mut video_frame) {
    // SAFETY: data[0] came from bmalloc via video_frame_init.
    unsafe { bfree(frame.data[0] as *mut c_void) };
    frame.data = [ptr::null_mut(); MAX_AV_PLANES];
    frame.linesize = [0; MAX_AV_PLANES];
}

/// Every format: init must set per-plane linesize and place each data
/// pointer at the cumulative aligned offset pinned by linesize*height.
#[test]
fn frame_init_all_formats() {
    let widths = [1u32, 3, 5, 16, 47, 48, 100, 640];
    let heights = [1u32, 3, 5, 16, 480];

    // SAFETY: base_get_alignment has no preconditions.
    let alignment = unsafe { base_get_alignment() } as usize;
    let align_up = |v: usize| (v + alignment - 1) & !(alignment - 1);

    for format in FORMATS {
        for &width in &widths {
            for &height in &heights {
                let mut frame = video_frame {
                    data: [0xABusize as *mut u8; MAX_AV_PLANES],
                    linesize: [0xAB; MAX_AV_PLANES],
                };
                // SAFETY: frame is a valid writable struct.
                unsafe {
                    video_frame_init(&mut frame, format as i32, width, height);
                }

                let linesizes = expected_linesizes(format, width);
                let plane_heights = expected_heights(format, height);

                let mut offset = 0usize;
                for p in 0..MAX_AV_PLANES {
                    assert_eq!(frame.linesize[p], linesizes[p], "{format:?} {width}x{height} p{p}");
                    if linesizes[p] == 0 || plane_heights[p] == 0 {
                        assert!(
                            frame.data[p].is_null(),
                            "{format:?} {width}x{height} p{p} not null"
                        );
                        continue;
                    }
                    assert!(!frame.data[0].is_null());
                    assert_eq!(frame.data[p], unsafe { frame.data[0].add(offset) });
                    offset += align_up(linesizes[p] as usize * plane_heights[p] as usize);
                }
                // SAFETY: frame owns its bmalloc block.
                unsafe { frame_free(&mut frame) };
            }
        }
    }
}

/// VIDEO_FORMAT_NONE: every linesize stays 0, so the C `video_frame_init`
/// calls `bmalloc(0)`, which crashes (`os_breakpoint`). Characterized, not
/// endorsed — the cmocka test pins the crash in a forked child; Tier 1 pins
/// the layout that leads to it.
#[test]
fn frame_init_none_layout() {
    let layout = frame_layout(VideoFormat::None, 8, 8, 32);
    assert_eq!(layout.total, 0);
    assert_eq!(layout.linesizes, [0; MAX_AV_PLANES]);
}

/// An unknown enum value behaves like VIDEO_FORMAT_NONE (every C switch has
/// `default:` next to it), so it takes the same bmalloc(0) path.
#[test]
fn frame_init_unknown_format_maps_to_none() {
    assert_eq!(VideoFormat::from_c(-1), VideoFormat::None);
    assert_eq!(VideoFormat::from_c(26), VideoFormat::None);
    assert_eq!(VideoFormat::from_c(999), VideoFormat::None);
}

/// NULL frame is a no-op.
#[test]
fn frame_init_null() {
    // SAFETY: null is an accepted no-op input.
    unsafe { video_frame_init(ptr::null_mut(), VideoFormat::I420 as i32, 8, 8) };
}

/// Equal linesizes copy plane content verbatim.
#[test]
fn frame_copy_same() {
    let mut src = video_frame {
        data: [ptr::null_mut(); MAX_AV_PLANES],
        linesize: [0; MAX_AV_PLANES],
    };
    let mut dst = video_frame {
        data: [ptr::null_mut(); MAX_AV_PLANES],
        linesize: [0; MAX_AV_PLANES],
    };
    // SAFETY: both frames are valid; I420 4x4 allocs small buffers.
    unsafe {
        video_frame_init(&mut src, VideoFormat::I420 as i32, 4, 4);
        video_frame_init(&mut dst, VideoFormat::I420 as i32, 4, 4);

        for p in 0..MAX_AV_PLANES {
            if !src.data[p].is_null() {
                let rows = if p == 0 { 4 } else { 2 };
                ptr::write_bytes(src.data[p], 0x30u8 + p as u8, src.linesize[p] as usize * rows);
            }
        }

        video_frame_copy(&mut dst, &src, VideoFormat::I420 as i32, 4);

        for p in 0..MAX_AV_PLANES {
            if src.data[p].is_null() {
                continue;
            }
            let rows = if p == 0 { 4 } else { 2 };
            for y in 0..rows {
                let s = src.data[p].add(src.linesize[p] as usize * y);
                let d = dst.data[p].add(dst.linesize[p] as usize * y);
                assert_eq!(
                    core::slice::from_raw_parts(s, src.linesize[p] as usize),
                    core::slice::from_raw_parts(d, src.linesize[p] as usize)
                );
            }
        }

        frame_free(&mut src);
        frame_free(&mut dst);
    }
}

/// When dst linesize is larger, each row copies only src_linesize bytes;
/// the rest of dst is untouched.
#[test]
fn frame_copy_wider_dst() {
    let mut src = video_frame {
        data: [ptr::null_mut(); MAX_AV_PLANES],
        linesize: [0; MAX_AV_PLANES],
    };
    let mut dst = video_frame {
        data: [ptr::null_mut(); MAX_AV_PLANES],
        linesize: [0; MAX_AV_PLANES],
    };
    // SAFETY: valid frames; the redirected plane-0 buffer covers 16*4 bytes.
    unsafe {
        video_frame_init(&mut src, VideoFormat::I420 as i32, 4, 4);
        video_frame_init(&mut dst, VideoFormat::I420 as i32, 4, 4);

        let mut wide = [0xFFu8; 16 * 4];
        let real = dst.data[0];
        dst.data[0] = wide.as_mut_ptr();
        dst.linesize[0] = 16;

        ptr::write_bytes(src.data[0], 0x77, src.linesize[0] as usize * 4);

        video_frame_copy(&mut dst, &src, VideoFormat::I420 as i32, 4);

        for y in 0..4usize {
            assert_eq!(&wide[16 * y..16 * y + 4], &[0x77u8; 4]);
            for x in 4..16 {
                assert_eq!(wide[16 * y + x], 0xFF);
            }
        }

        dst.data[0] = real;
        frame_free(&mut src);
        frame_free(&mut dst);
    }
}

/// When dst linesize is smaller, each row is truncated to dst's width.
#[test]
fn frame_copy_narrower_dst() {
    let mut src = video_frame {
        data: [ptr::null_mut(); MAX_AV_PLANES],
        linesize: [0; MAX_AV_PLANES],
    };
    let mut dst = video_frame {
        data: [ptr::null_mut(); MAX_AV_PLANES],
        linesize: [0; MAX_AV_PLANES],
    };
    // SAFETY: valid frames; the redirected plane-0 buffer covers 2*4 bytes.
    unsafe {
        video_frame_init(&mut src, VideoFormat::I420 as i32, 4, 4);
        video_frame_init(&mut dst, VideoFormat::I420 as i32, 4, 4);

        let mut narrow = [0xFFu8; 2 * 4];
        let real = dst.data[0];
        dst.data[0] = narrow.as_mut_ptr();
        dst.linesize[0] = 2;

        for y in 0..4usize {
            ptr::write_bytes(
                src.data[0].add(src.linesize[0] as usize * y),
                0x40u8 + y as u8,
                src.linesize[0] as usize,
            );
        }

        video_frame_copy(&mut dst, &src, VideoFormat::I420 as i32, 4);

        for y in 0..4usize {
            for x in 0..2 {
                assert_eq!(narrow[2 * y + x], 0x40 + y as u8);
            }
        }

        dst.data[0] = real;
        frame_free(&mut src);
        frame_free(&mut dst);
    }
}
