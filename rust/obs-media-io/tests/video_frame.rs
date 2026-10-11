//! Tier 1: safe-API tests mirroring `test/cmocka/test_video_frame.c`, plus
//! edge cases.
//!
//! The C test's null frame (`frame_init_null`) and null `video_frame_free`
//! / `video_frame_destroy` have no safe-API counterpart; the Tier 3 parity
//! test covers them through the shims. Its `frame_copy_wider_dst` and
//! `frame_copy_narrower_dst` swap plane 0 of a frame for a foreign buffer,
//! so they test `copy_plane` on plane 0 directly.

use obs_c_oracle as _;
use obs_media_io::video_frame::{
    FrameLayout, VideoFrame, copy_extent, copy_plane, linesizes, plane_heights,
};
use obs_media_io::video_io::{MAX_AV_PLANES, VideoFormat};
use obs_util::bmem::ALIGNMENT;
use proptest as _;

/// As the C test's `expected_linesizes`.
fn expected_linesizes(format: VideoFormat, width: u32) -> [u32; MAX_AV_PLANES] {
    let mut out = [0; MAX_AV_PLANES];
    let half = width.div_ceil(2);
    match format {
        VideoFormat::None => {}
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
            let w = (width + 47) & !47u32; // aligned up to 48
            out[0] = w.div_ceil(6) * 16;
        }
    }
    out
}

/// As the C test's `expected_heights`.
fn expected_heights(format: VideoFormat, height: u32) -> [u32; MAX_AV_PLANES] {
    let mut out = [0; MAX_AV_PLANES];
    let half = height.div_ceil(2);
    match format {
        VideoFormat::None => {}
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
    }
    out
}

/// As the C test's `align_up`.
fn align_up(v: usize) -> usize {
    (v + ALIGNMENT - 1) & !(ALIGNMENT - 1)
}

/// `frame_init_all_formats`: every format sets each plane's linesize and
/// places each plane at the cumulative aligned offset.
#[test]
fn frame_init_all_formats() {
    const WIDTHS: [u32; 8] = [1, 3, 5, 16, 47, 48, 100, 640];
    const HEIGHTS: [u32; 5] = [1, 3, 5, 16, 480];

    // VIDEO_FORMAT_NONE allocs 0 bytes and crashes (frame_init_none).
    for format in VideoFormat::ALL.into_iter().skip(1) {
        for width in WIDTHS {
            for height in HEIGHTS {
                let frame = VideoFrame::new(format, width, height);
                let layout = frame.layout();

                let linesizes = expected_linesizes(format, width);
                let plane_heights = expected_heights(format, height);

                let mut offset = 0;
                for p in 0..MAX_AV_PLANES {
                    let ctx = format!("{format:?} {width}x{height} plane {p}");
                    assert_eq!(frame.linesize(p), linesizes[p], "{ctx}");
                    if linesizes[p] == 0 || plane_heights[p] == 0 {
                        assert_eq!(layout.offset[p], None, "{ctx}");
                        assert!(frame.plane(p).is_none(), "{ctx}");
                        continue;
                    }
                    assert!(frame.plane(0).is_some(), "{ctx}");
                    assert_eq!(layout.offset[p], Some(offset), "{ctx}");
                    offset += align_up(linesizes[p] as usize * plane_heights[p] as usize);
                }
                assert_eq!(layout.size, offset, "{format:?} {width}x{height}");
            }
        }
    }
}

/// `frame_init_none`: every linesize of VIDEO_FORMAT_NONE stays 0, so init
/// allocates 0 bytes, which crashes in C (`bmalloc(0)`) and panics here.
/// Characterized, not endorsed.
#[test]
#[should_panic(expected = "Allocating 0 bytes")]
fn frame_init_none() {
    let _ = VideoFrame::new(VideoFormat::None, 8, 8);
}

#[test]
fn frame_init_none_layout_is_empty() {
    let layout = FrameLayout::new(VideoFormat::None, 8, 8);
    assert_eq!(layout.size, 0);
    assert_eq!(layout.linesize, [0; MAX_AV_PLANES]);
    let mut offset = [None; MAX_AV_PLANES];
    offset[0] = Some(0);
    assert_eq!(layout.offset, offset);
}

/// Fills `rows` rows of each I420 plane of `frame` with `0x30 + plane`, as
/// `frame_copy_same` does.
fn fill_i420(frame: &mut VideoFrame, rows: [u32; 3]) {
    for (p, rows) in rows.into_iter().enumerate() {
        let len = (frame.linesize(p) * rows) as usize;
        frame.plane_mut(p).unwrap()[..len].fill(0x30 + p as u8);
    }
}

/// `frame_copy_same`: equal linesizes copy plane content verbatim.
#[test]
fn frame_copy_same() {
    let mut src = VideoFrame::new(VideoFormat::I420, 4, 4);
    let mut dst = VideoFrame::new(VideoFormat::I420, 4, 4);
    fill_i420(&mut src, [4, 2, 2]);

    dst.copy_from(&src, VideoFormat::I420, 4);

    for (p, rows) in [4u32, 2, 2].into_iter().enumerate() {
        let (sl, dl) = (src.linesize(p) as usize, dst.linesize(p) as usize);
        let (s, d) = (src.plane(p).unwrap(), dst.plane(p).unwrap());
        for y in 0..rows as usize {
            assert_eq!(&d[dl * y..][..sl], &s[sl * y..][..sl], "plane {p} row {y}");
        }
    }
    for p in 3..MAX_AV_PLANES {
        assert!(src.plane(p).is_none());
    }
}

/// `frame_copy_wider_dst`: when dst's linesize is larger, each row copies
/// only src's linesize; the rest of dst is untouched.
#[test]
fn frame_copy_wider_dst() {
    let mut src = VideoFrame::new(VideoFormat::I420, 4, 4);
    let mut wide = [0xffu8; 16 * 4];
    src.plane_mut(0).unwrap()[..4 * 4].fill(0x77);

    let rows = plane_heights(VideoFormat::I420, 4)[0];
    copy_plane(&mut wide, 16, src.plane(0).unwrap(), src.linesize(0), rows);

    for y in 0..4 {
        assert_eq!(&wide[16 * y..][..4], &src.plane(0).unwrap()[..4]);
        assert!(wide[16 * y + 4..16 * (y + 1)].iter().all(|&b| b == 0xff));
    }
}

/// `frame_copy_narrower_dst`: when dst's linesize is smaller, each row is
/// truncated to dst's width.
#[test]
fn frame_copy_narrower_dst() {
    let mut src = VideoFrame::new(VideoFormat::I420, 4, 4);
    let mut narrow = [0xffu8; 2 * 4];
    let linesize = src.linesize(0) as usize;
    let plane = src.plane_mut(0).unwrap();
    for y in 0..4 {
        plane[linesize * y..][..linesize].fill(0x40 + y as u8);
    }

    let rows = plane_heights(VideoFormat::I420, 4)[0];
    copy_plane(&mut narrow, 2, src.plane(0).unwrap(), src.linesize(0), rows);

    for y in 0..4 {
        assert_eq!(&narrow[2 * y..][..2], &[0x40 + y as u8; 2]);
    }
}

/// `frame_inline_helpers`: `video_frame_create` / `video_frame_destroy`.
#[test]
fn frame_inline_helpers() {
    let frame = VideoFrame::new(VideoFormat::Bgra, 2, 2);
    assert!(frame.plane(0).is_some());
    assert_eq!(frame.linesize(0), 8);
    drop(frame);
}

// Edge cases beyond the C test.

#[test]
fn every_linesize_and_height_matches_the_c_test_model() {
    for format in VideoFormat::ALL {
        for n in [0, 1, 2, 7, 47, 48, 49, 95, 96, 97, 1920, 4096] {
            assert_eq!(
                linesizes(format, n),
                expected_linesizes(format, n),
                "{format:?} {n}"
            );
            assert_eq!(
                plane_heights(format, n),
                expected_heights(format, n),
                "{format:?} {n}"
            );
        }
    }
}

/// Characterized, not endorsed: V210 "aligns" to 48 with a power-of-two
/// mask: `!47` clears bits 0-3 and bit 5, so 1 rounds to 16 and 48 to 80.
#[test]
fn v210_align_quirk() {
    assert_eq!(linesizes(VideoFormat::V210, 1)[0], 48); // 16, not 48
    assert_eq!(linesizes(VideoFormat::V210, 47)[0], 224); // 80, not 48
    assert_eq!(linesizes(VideoFormat::V210, 48)[0], 224); // 80, not 48
    assert_eq!(linesizes(VideoFormat::V210, 1280)[0], 3424); // not 3456
    assert_eq!(linesizes(VideoFormat::V210, 1920)[0], 5120);
}

#[test]
fn v210_aligned_width_is_never_below_width() {
    for width in (0..1 << 16).chain([u32::MAX / 4, u32::MAX / 2]) {
        let aligned = width.wrapping_add(47) & !47;
        assert!(aligned >= width, "{width}");
    }
}

/// `u32` math wraps like C: `width * 4` and `HALF(UINT32_MAX)`.
#[test]
fn linesizes_wrap_like_c() {
    assert_eq!(linesizes(VideoFormat::Bgra, 1 << 30)[0], 0);
    assert_eq!(linesizes(VideoFormat::Bgr3, u32::MAX)[0], u32::MAX - 2);
    assert_eq!(
        linesizes(VideoFormat::I420, u32::MAX),
        [u32::MAX, 0, 0, 0, 0, 0, 0, 0]
    );
    assert_eq!(
        plane_heights(VideoFormat::Nv12, u32::MAX),
        [u32::MAX, 0, 0, 0, 0, 0, 0, 0]
    );
    // u32::MAX + 47 wraps to 46, which the mask clears to 0.
    assert_eq!(linesizes(VideoFormat::V210, u32::MAX)[0], 0);
    // 0xFFFF_FFC0 stays put; (+5) / 6 * 16 wraps.
    assert_eq!(
        linesizes(VideoFormat::V210, u32::MAX - 63)[0],
        0xFFFF_FFC0u32.div_ceil(6).wrapping_mul(16)
    );
}

#[test]
fn zero_width_or_height_layout_is_empty() {
    for format in VideoFormat::ALL {
        assert_eq!(FrameLayout::new(format, 0, 8).size, 0, "{format:?}");
        assert_eq!(FrameLayout::new(format, 8, 0).size, 0, "{format:?}");
    }
}

/// Characterized, not endorsed: a plane after a skipped one starts at
/// offset 0, on top of plane 0 (I40A at width `u32::MAX`: planes 1 and 2
/// wrap to linesize 0).
#[test]
fn plane_after_a_skipped_plane_lands_on_plane_0() {
    let layout = FrameLayout::new(VideoFormat::I40a, u32::MAX, 2);
    assert_eq!(layout.linesize, [u32::MAX, 0, 0, u32::MAX, 0, 0, 0, 0]);
    let plane = align_up(u32::MAX as usize * 2);
    assert_eq!(layout.size, 2 * plane);
    assert_eq!(
        layout.offset,
        [Some(0), None, None, Some(0), None, None, None, None]
    );
}

#[test]
fn plane_sizes_do_not_wrap_in_usize() {
    let layout = FrameLayout::new(VideoFormat::Bgra, 1 << 20, 1 << 20);
    assert_eq!(layout.size, 1 << 42);
}

#[test]
fn copy_extent_equal_linesizes_wraps_in_u32() {
    assert_eq!(copy_extent(8, 8, 4), (32, 32));
    assert_eq!(copy_extent(1 << 16, 1 << 16, 1 << 16), (0, 0));
    assert_eq!(copy_extent(0, 0, 4), (0, 0));
}

#[test]
fn copy_extent_different_linesizes_ends_at_last_row() {
    assert_eq!(copy_extent(16, 4, 4), (52, 16));
    assert_eq!(copy_extent(2, 4, 4), (8, 14));
    assert_eq!(copy_extent(0, 4, 4), (0, 0));
    assert_eq!(copy_extent(16, 4, 0), (0, 0));
}

#[test]
fn copy_plane_with_equal_linesizes_copies_one_block() {
    let src: Vec<u8> = (0..32).collect();
    let mut dst = [0xffu8; 40];
    copy_plane(&mut dst, 8, &src, 8, 4);
    assert_eq!(&dst[..32], &src[..]);
    assert!(dst[32..].iter().all(|&b| b == 0xff));
}

#[test]
fn copy_from_skips_planes_the_format_lacks() {
    let mut src = VideoFrame::new(VideoFormat::I420, 4, 4);
    let mut dst = VideoFrame::new(VideoFormat::I420, 4, 4);
    fill_i420(&mut src, [4, 2, 2]);

    // NV12 has two planes, so plane 2 stays zero.
    dst.copy_from(&src, VideoFormat::Nv12, 4);
    assert!(dst.plane(0).unwrap()[..16].iter().all(|&b| b == 0x30));
    assert!(dst.plane(1).unwrap()[..2 * 2].iter().all(|&b| b == 0x31));
    assert!(dst.plane(2).unwrap()[..2 * 2].iter().all(|&b| b == 0));

    // NONE copies nothing.
    let mut dst = VideoFrame::new(VideoFormat::I420, 4, 4);
    dst.copy_from(&src, VideoFormat::None, 4);
    assert!(dst.plane(0).unwrap().iter().all(|&b| b == 0));
}

#[test]
fn copy_from_zero_rows_copies_nothing() {
    let mut src = VideoFrame::new(VideoFormat::I420, 4, 4);
    let mut dst = VideoFrame::new(VideoFormat::I420, 4, 4);
    fill_i420(&mut src, [4, 2, 2]);
    dst.copy_from(&src, VideoFormat::I420, 0);
    assert!(dst.plane(0).unwrap().iter().all(|&b| b == 0));
}

#[test]
fn copy_from_wider_frame_truncates_rows() {
    let mut src = VideoFrame::new(VideoFormat::Y800, 8, 2);
    let mut dst = VideoFrame::new(VideoFormat::Y800, 4, 2);
    src.plane_mut(0).unwrap()[..16].copy_from_slice(&[
        1, 2, 3, 4, 5, 6, 7, 8, //
        9, 10, 11, 12, 13, 14, 15, 16,
    ]);
    dst.copy_from(&src, VideoFormat::Y800, 2);
    assert_eq!(&dst.plane(0).unwrap()[..8], &[1, 2, 3, 4, 9, 10, 11, 12]);
}
