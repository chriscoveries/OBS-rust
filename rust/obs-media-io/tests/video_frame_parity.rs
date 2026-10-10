//! Tier 3 differential test: the Rust `video-frame.c` port must behave
//! identically to the original C (compiled into `obs-c-oracle` with
//! `oracle_` prefixes) on every input: same linesize/height tables, same
//! plane offsets, same copy output.
//!
//! Excluded inputs: formats with no planes (VIDEO_FORMAT_NONE and unknown
//! values) and zero width/height — those make both sides call bmalloc(0),
//! which aborts; the crash is pinned by the cmocka characterization test.
//! Allocations are bounded (width <= 640, height <= 480) so the init parity
//! case stays small.

use obs_c_oracle as _; // links the test bmalloc/bfree (oracle/test_bmem.c)

use core::ffi::c_void;
use core::ptr;

use obs_c_oracle::video_frame::{
    OracleVideoFrame, oracle_video_frame_copy, oracle_video_frame_get_linesizes,
    oracle_video_frame_get_plane_heights, oracle_video_frame_init,
};
use obs_media_io::ffi::video_frame::{video_frame, video_frame_copy, video_frame_init};
use obs_media_io::video_frame::{MAX_AV_PLANES, VideoFormat, frame_layout};

use proptest::prelude::*;

unsafe extern "C" {
    fn bfree(ptr: *mut c_void);
    fn base_get_alignment() -> i32;
}

fn any_format() -> impl Strategy<Value = i32> {
    // Every real discriminant plus values outside the enum range, which must
    // take the C `default:` (VIDEO_FORMAT_NONE) path.
    prop::sample::select(vec![
        -1,
        0,
        1,
        2,
        3,
        4,
        5,
        6,
        7,
        8,
        9,
        10,
        11,
        12,
        13,
        14,
        15,
        16,
        17,
        18,
        19,
        20,
        21,
        22,
        23,
        24,
        25,
        26,
        42,
        999,
        i32::MIN,
        i32::MAX,
    ])
}

fn alloc_format() -> impl Strategy<Value = VideoFormat> {
    // Formats whose init actually allocates (every enum member but NONE).
    (1i32..=25).prop_map(VideoFormat::from_c)
}

proptest! {
    /// Linesize and plane-height tables must match the oracle on every
    /// format value (including out-of-range ones) and any width/height,
    /// covering the wrapping u32 arithmetic.
    #[test]
    fn linesize_and_height_tables_parity(
        format in any_format(),
        width in any::<u32>(),
        height in any::<u32>(),
    ) {
        let mut oracle_ls = [0u32; MAX_AV_PLANES];
        let mut oracle_h = [0u32; MAX_AV_PLANES];
        // SAFETY: arrays are valid MAX_AV_PLANES-sized out buffers.
        unsafe {
            oracle_video_frame_get_linesizes(oracle_ls.as_mut_ptr(), format, width);
            oracle_video_frame_get_plane_heights(oracle_h.as_mut_ptr(), format, height);
        }
        let fmt = VideoFormat::from_c(format);
        prop_assert_eq!(
            obs_media_io::video_frame::get_linesizes(fmt, width),
            oracle_ls,
            "linesize fmt={} w={}", format, width
        );
        prop_assert_eq!(
            obs_media_io::video_frame::get_plane_heights(fmt, height),
            oracle_h,
            "heights fmt={} h={}", format, height
        );
    }

    /// video_frame_init must produce identical linesizes and identical
    /// plane-pointer offsets relative to data[0].
    #[test]
    fn frame_init_parity(
        format in alloc_format(),
        width in 1u32..=640,
        height in 1u32..=480,
    ) {
        let mut rust = video_frame {
            data: [ptr::null_mut(); MAX_AV_PLANES],
            linesize: [0; MAX_AV_PLANES],
        };
        let mut oracle = OracleVideoFrame {
            data: [ptr::null_mut(); MAX_AV_PLANES],
            linesize: [0; MAX_AV_PLANES],
        };
        let fmt = format as i32;
        // SAFETY: both structs are valid writable frames; their data[0]
        // blocks are freed below.
        unsafe {
            video_frame_init(&mut rust, fmt, width, height);
            oracle_video_frame_init(&mut oracle, fmt, width, height);

            prop_assert_eq!(rust.linesize, oracle.linesize);
            for i in 0..MAX_AV_PLANES {
                prop_assert_eq!(
                    rust.data[i].is_null(),
                    oracle.data[i].is_null(),
                    "plane {} nullness differs", i
                );
                if !rust.data[i].is_null() {
                    prop_assert_eq!(
                        rust.data[i].offset_from(rust.data[0]),
                        oracle.data[i].offset_from(oracle.data[0]),
                        "plane {} offset differs", i
                    );
                }
            }

            bfree(rust.data[0] as *mut c_void);
            bfree(oracle.data[0] as *mut c_void);
        }
    }

    /// video_frame_copy: identical bytes on both sides, on the contiguous
    /// path (equal linesizes, or remaining planes) and on the line-by-line
    /// path (plane 0 patched to a wider or narrower scratch buffer).
    #[test]
    fn frame_copy_parity(
        format in alloc_format(),
        width in 1u32..=640,
        height in 1u32..=480,
        src_fill in proptest::collection::vec(any::<u8>(), 1..=64),
        dst_ls_delta in -32i32..=64,
    ) {
        let fmt = format as i32;
        // SAFETY: base_get_alignment has no preconditions.
        let alignment = unsafe { base_get_alignment() } as usize;
        let layout = frame_layout(format, width, height, alignment);
        prop_assume!(layout.total > 0);
        let scratch_ls = (layout.linesizes[0] as i64 + i64::from(dst_ls_delta)).max(0) as u32;
        prop_assume!(scratch_ls > 0);

        let mut rust_src = video_frame { data: [ptr::null_mut(); MAX_AV_PLANES], linesize: [0; MAX_AV_PLANES] };
        let mut rust_dst = video_frame { data: [ptr::null_mut(); MAX_AV_PLANES], linesize: [0; MAX_AV_PLANES] };
        let mut orc_src = OracleVideoFrame { data: [ptr::null_mut(); MAX_AV_PLANES], linesize: [0; MAX_AV_PLANES] };
        let mut orc_dst = OracleVideoFrame { data: [ptr::null_mut(); MAX_AV_PLANES], linesize: [0; MAX_AV_PLANES] };

        // SAFETY: all four frames are initialized with the same dimensions,
        // filled with identical patterns, copied, compared and freed.
        unsafe {
            video_frame_init(&mut rust_src, fmt, width, height);
            video_frame_init(&mut rust_dst, fmt, width, height);
            oracle_video_frame_init(&mut orc_src, fmt, width, height);
            oracle_video_frame_init(&mut orc_dst, fmt, width, height);

            // Identical src contents and dst pre-fill on both sides.
            for i in 0..layout.total {
                let v = src_fill[i % src_fill.len()];
                *rust_src.data[0].add(i) = v;
                *orc_src.data[0].add(i) = v;
            }
            ptr::write_bytes(rust_dst.data[0], 0xCC, layout.total);
            ptr::write_bytes(orc_dst.data[0], 0xCC, layout.total);

            // Route plane 0 of each dst through scratch buffers with a
            // possibly different linesize; other planes stay in place and
            // exercise the contiguous path when their linesizes match.
            let rows0 = layout.heights[0] as usize;
            let mut rust_scratch = vec![0xEEu8; scratch_ls as usize * rows0];
            let mut orc_scratch = vec![0xEEu8; scratch_ls as usize * rows0];
            let rust_dst_real = rust_dst.data[0];
            let orc_dst_real = orc_dst.data[0];
            rust_dst.data[0] = rust_scratch.as_mut_ptr();
            rust_dst.linesize[0] = scratch_ls;
            orc_dst.data[0] = orc_scratch.as_mut_ptr();
            orc_dst.linesize[0] = scratch_ls;

            video_frame_copy(&mut rust_dst, &rust_src, fmt, height);
            oracle_video_frame_copy(&mut orc_dst, &orc_src, fmt, height);

            prop_assert_eq!(rust_scratch, orc_scratch, "plane0 scratch differs");

            let rust_buf = core::slice::from_raw_parts(rust_dst_real, layout.total);
            let orc_buf = core::slice::from_raw_parts(orc_dst_real, layout.total);
            prop_assert_eq!(rust_buf, orc_buf, "remaining dst contents differ");

            bfree(rust_src.data[0] as *mut c_void);
            bfree(rust_dst_real as *mut c_void);
            bfree(orc_src.data[0] as *mut c_void);
            bfree(orc_dst_real as *mut c_void);
        }
    }
}
