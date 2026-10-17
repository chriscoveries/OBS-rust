//! Tier 3: the Rust C ABI shims and safe core behave exactly like the
//! original C, compiled as an oracle.
//!
//! No intentional differences from C. Exclusions, each one a crash or
//! undefined behaviour in C:
//!
//! - `video_frame_init` (and `video_frame_create`) where no plane has both a
//!   linesize and a height: `VIDEO_FORMAT_NONE`, a value outside
//!   `enum video_format`, or a zero width or height. Both sides reach
//!   `bmalloc(0)`, which crashes; `test/cmocka/test_video_frame.c` checks
//!   that in a child process. The test decides this from the oracle's
//!   linesizes and heights.
//! - `video_frame_init` with a layout too large to allocate. The linesize
//!   and height sweep covers the `u32` wraps over the whole range instead,
//!   and Tier 1 covers the layouts they lead to.
//! - `video_frame_copy` with planes too small for `cy` rows, or
//!   overlapping: C reads or writes out of bounds.
//!
//! Frames are compared as linesizes plus each plane pointer's offset from
//! `data[0]` (or null). Copies run on separate, canary-filled destinations
//! and compare every byte of them, so a write outside the copied rows shows
//! up too.
//!
//! Reminder: the mutation check (break the core, see this file fail) is done
//! once per port, not on every change.

use core::ffi::{c_int, c_void};
use core::{ptr, slice};

use obs_c_oracle::video_frame as c;
use obs_media_io::ffi::video_frame::{self as rs, video_frame};
use obs_media_io::video_frame::{FrameLayout, linesizes, plane_heights};
use obs_media_io::video_io::{MAX_AV_PLANES, VideoFormat, video_format_from_c, video_format_to_c};
use obs_util::ffi::bmem::bfree;
use proptest::prelude::*;

/// Every enumerator plus values just outside `enum video_format`.
fn formats() -> impl Iterator<Item = c_int> {
    VideoFormat::ALL.into_iter().map(video_format_to_c).chain([
        -1,
        26,
        27,
        100,
        c_int::MIN,
        c_int::MAX,
    ])
}

/// Widths and heights around every rounding step and `u32` wrap.
const EDGES: [u32; 22] = [
    0,
    1,
    2,
    3,
    5,
    6,
    47,
    48,
    49,
    95,
    1280,
    1920,
    1 << 15,
    1 << 16,
    (1 << 30) - 1,
    1 << 30,
    (1 << 31) - 1,
    1 << 31,
    u32::MAX - 63,
    u32::MAX - 47,
    u32::MAX - 1,
    u32::MAX,
];

fn c_linesizes(format: c_int, width: u32) -> [u32; MAX_AV_PLANES] {
    let mut out = [0; MAX_AV_PLANES];
    // SAFETY: `out` holds MAX_AV_PLANES zeroed u32s.
    unsafe { c::oracle_video_frame_get_linesizes(out.as_mut_ptr(), format, width) };
    out
}

fn c_heights(format: c_int, height: u32) -> [u32; MAX_AV_PLANES] {
    let mut out = [0; MAX_AV_PLANES];
    // SAFETY: as above.
    unsafe { c::oracle_video_frame_get_plane_heights(out.as_mut_ptr(), format, height) };
    out
}

/// The safe core, with the shims' conversion of a C format value.
fn rs_format(format: c_int) -> VideoFormat {
    video_format_from_c(format).unwrap_or(VideoFormat::None)
}

fn check_sizes(format: c_int, n: u32) {
    assert_eq!(
        linesizes(rs_format(format), n),
        c_linesizes(format, n),
        "linesizes {format} {n}"
    );
    assert_eq!(
        plane_heights(rs_format(format), n),
        c_heights(format, n),
        "heights {format} {n}"
    );
}

/// Whether `video_frame_init` reaches `bmalloc(0)` in C.
fn allocates_nothing(format: c_int, width: u32, height: u32) -> bool {
    let (l, h) = (c_linesizes(format, width), c_heights(format, height));
    (0..MAX_AV_PLANES).all(|i| l[i] == 0 || h[i] == 0)
}

/// A frame's observable state: linesizes, and each plane's offset from
/// `data[0]` (`None` for null).
type Shape = ([u32; MAX_AV_PLANES], [Option<usize>; MAX_AV_PLANES]);

fn shape(frame: &video_frame) -> Shape {
    let base = frame.data[0] as usize;
    let offsets = frame
        .data
        .map(|p| (!p.is_null()).then(|| (p as usize).wrapping_sub(base)));
    (frame.linesize, offsets)
}

/// A frame filled with 0xAB bytes, as the C test starts from.
fn garbage_frame() -> video_frame {
    video_frame {
        data: [usize::from_ne_bytes([0xab; size_of::<usize>()]) as *mut u8; MAX_AV_PLANES],
        linesize: [0xabab_abab; MAX_AV_PLANES],
    }
}

fn init_with(init: impl FnOnce(*mut video_frame)) -> Shape {
    let mut frame = garbage_frame();
    init(&mut frame);
    let out = shape(&frame);
    // SAFETY: init allocated `data[0]` with bmalloc.
    unsafe { bfree(frame.data[0].cast()) };
    out
}

fn check_init(format: c_int, width: u32, height: u32) {
    if allocates_nothing(format, width, height) {
        return;
    }
    let ctx = format!("{format} {width}x{height}");
    // SAFETY: the frame is a valid, writable `struct video_frame`.
    let c = init_with(|f| unsafe { c::oracle_video_frame_init(f.cast(), format, width, height) });
    // SAFETY: as above.
    let r = init_with(|f| unsafe { rs::video_frame_init(f, format, width, height) });
    assert_eq!(r, c, "{ctx}");
}

/// Plane buffers for one side of a copy: one `Vec` per plane, each `len`
/// bytes long, pointed at by a `video_frame` with the given linesizes.
struct Planes {
    bufs: Vec<Vec<u8>>,
    linesize: [u32; MAX_AV_PLANES],
}

impl Planes {
    fn new(
        linesize: [u32; MAX_AV_PLANES],
        rows: [u32; MAX_AV_PLANES],
        fill: impl Fn(usize, usize) -> u8,
    ) -> Self {
        let bufs = (0..MAX_AV_PLANES)
            .map(|p| {
                // A little slack past the rows, so a stray write lands in the buffer.
                let len = linesize[p] as usize * rows[p] as usize + 16;
                (0..len).map(|i| fill(p, i)).collect()
            })
            .collect();
        Self { bufs, linesize }
    }

    fn frame(&mut self) -> video_frame {
        let mut data = [ptr::null_mut(); MAX_AV_PLANES];
        for (d, buf) in data.iter_mut().zip(&mut self.bufs) {
            *d = buf.as_mut_ptr();
        }
        video_frame {
            data,
            linesize: self.linesize,
        }
    }
}

/// Copies `cy` rows of `format` from a patterned source into two
/// canary-filled destinations, one through the oracle and one through the
/// shim, and compares every destination byte.
fn check_copy(format: c_int, cy: u32, src_ls: [u32; MAX_AV_PLANES], dst_ls: [u32; MAX_AV_PLANES]) {
    let rows = c_heights(format, cy);
    let mut src = Planes::new(src_ls, rows, |p, i| (p * 37 + i * 7 + 1) as u8);
    let canary = |_, i: usize| 0x5a ^ i as u8;
    let mut dst_c = Planes::new(dst_ls, rows, canary);
    let mut dst_r = Planes::new(dst_ls, rows, canary);

    let src_frame = src.frame();
    let mut c_frame = dst_c.frame();
    let mut r_frame = dst_r.frame();
    // SAFETY: every plane holds at least linesize * rows bytes for the rows
    // copied, and the buffers are distinct.
    unsafe {
        c::oracle_video_frame_copy(
            (&raw mut c_frame).cast(),
            (&raw const src_frame).cast(),
            format,
            cy,
        );
        rs::video_frame_copy(&mut r_frame, &src_frame, format, cy);
    }
    assert_eq!(
        dst_r.bufs, dst_c.bufs,
        "format {format} cy {cy} src {src_ls:?} dst {dst_ls:?}"
    );
}

#[test]
fn linesizes_and_heights_sweep() {
    for format in formats() {
        for n in EDGES {
            check_sizes(format, n);
        }
    }
}

#[test]
fn init_sweep() {
    const WIDTHS: [u32; 12] = [0, 1, 2, 3, 5, 16, 47, 48, 49, 100, 640, 1281];
    const HEIGHTS: [u32; 7] = [0, 1, 2, 3, 5, 16, 481];
    for format in formats() {
        for width in WIDTHS {
            for height in HEIGHTS {
                check_init(format, width, height);
            }
        }
    }
}

#[test]
fn init_null_frame_is_a_no_op() {
    // SAFETY: a null frame is allowed.
    unsafe {
        c::oracle_video_frame_init(ptr::null_mut(), video_format_to_c(VideoFormat::I420), 8, 8);
        rs::video_frame_init(ptr::null_mut(), video_format_to_c(VideoFormat::I420), 8, 8);
    }
}

#[test]
fn copy_sweep() {
    // Equal, wider dst, narrower dst, and zero linesizes on either side.
    const PAIRS: [(u32, u32); 6] = [(4, 4), (4, 7), (7, 4), (0, 4), (4, 0), (0, 0)];
    for format in formats() {
        for cy in 0..6 {
            for (s, d) in PAIRS {
                check_copy(format, cy, [s; MAX_AV_PLANES], [d; MAX_AV_PLANES]);
            }
        }
    }
}

/// `frame_copy_same`, `frame_copy_wider_dst` and `frame_copy_narrower_dst`
/// from the C test, on frames from `video_frame_init`.
#[test]
fn copy_between_initialized_frames() {
    let i420 = video_format_to_c(VideoFormat::I420);
    let src_size = FrameLayout::new(VideoFormat::I420, 4, 4).size;
    for dst_width in [4, 2, 16] {
        let dst_size = FrameLayout::new(VideoFormat::I420, dst_width, 4).size;
        let mut src = garbage_frame();
        let (mut dst_c, mut dst_r) = (garbage_frame(), garbage_frame());
        // SAFETY: init gives each frame one block of its layout's size; every
        // frame is freed below.
        unsafe {
            rs::video_frame_init(&mut src, i420, 4, 4);
            rs::video_frame_init(&mut dst_c, i420, dst_width, 4);
            rs::video_frame_init(&mut dst_r, i420, dst_width, 4);
            let src_bytes = slice::from_raw_parts_mut(src.data[0], src_size);
            for (i, b) in src_bytes.iter_mut().enumerate() {
                *b = i as u8;
            }
            slice::from_raw_parts_mut(dst_c.data[0], dst_size).fill(0xff);
            slice::from_raw_parts_mut(dst_r.data[0], dst_size).fill(0xff);
            c::oracle_video_frame_copy((&raw mut dst_c).cast(), (&raw const src).cast(), i420, 4);
            rs::video_frame_copy(&mut dst_r, &src, i420, 4);
            assert_eq!(
                slice::from_raw_parts(dst_r.data[0], dst_size),
                slice::from_raw_parts(dst_c.data[0], dst_size),
                "dst width {dst_width}"
            );
            for frame in [src, dst_c, dst_r] {
                bfree(frame.data[0].cast());
            }
        }
    }
}

#[test]
fn inline_helpers_match_c() {
    for format in formats() {
        for (width, height) in [(1, 1), (2, 2), (5, 3), (47, 16), (640, 480)] {
            if allocates_nothing(format, width, height) {
                continue;
            }
            // SAFETY: each frame comes from create and goes to the matching
            // destroy.
            unsafe {
                let c_frame =
                    c::oracle_video_frame_create(format, width, height).cast::<video_frame>();
                let r_frame = rs::video_frame_create(format, width, height);
                assert_eq!(
                    shape(&*r_frame),
                    shape(&*c_frame),
                    "{format} {width}x{height}"
                );
                c::oracle_video_frame_destroy(c_frame.cast());
                rs::video_frame_destroy(r_frame);
            }
        }
    }
}

#[test]
fn free_zeroes_the_frame() {
    let bgra = video_format_to_c(VideoFormat::Bgra);
    let (mut c_frame, mut r_frame) = (garbage_frame(), garbage_frame());
    // SAFETY: init allocates `data[0]`, which free releases.
    unsafe {
        rs::video_frame_init(&mut c_frame, bgra, 2, 2);
        rs::video_frame_init(&mut r_frame, bgra, 2, 2);
        c::oracle_video_frame_free((&raw mut c_frame).cast::<c_void>());
        rs::video_frame_free(&mut r_frame);
    }
    assert_eq!(shape(&r_frame), shape(&c_frame));
    assert_eq!(shape(&r_frame), ([0; MAX_AV_PLANES], [None; MAX_AV_PLANES]));
}

#[test]
fn free_and_destroy_ignore_null() {
    // SAFETY: null is allowed.
    unsafe {
        c::oracle_video_frame_free(ptr::null_mut());
        c::oracle_video_frame_destroy(ptr::null_mut());
        rs::video_frame_free(ptr::null_mut());
        rs::video_frame_destroy(ptr::null_mut());
    }
}

fn any_format() -> impl Strategy<Value = c_int> {
    prop_oneof![4 => -2..30, 1 => any::<c_int>()]
}

fn linesizes_strategy() -> impl Strategy<Value = [u32; MAX_AV_PLANES]> {
    prop_oneof![
        (0u32..24).prop_map(|l| [l; MAX_AV_PLANES]),
        prop::array::uniform8(0u32..24),
    ]
}

proptest! {
    #[test]
    fn linesizes_and_heights_match_c(format in any_format(), n in any::<u32>()) {
        check_sizes(format, n);
    }

    #[test]
    fn init_matches_c(format in 1..=25, width in 1u32..=4096, height in 1u32..=1100) {
        check_init(format, width, height);
    }

    #[test]
    fn copy_matches_c(
        format in any_format(),
        cy in 0u32..12,
        src_ls in linesizes_strategy(),
        dst_ls in linesizes_strategy(),
    ) {
        check_copy(format, cy, src_ls, dst_ls);
    }
}
