//! Tier 2 layout test: `struct video_frame` in `obs_media_io::ffi` must have
//! the same size, alignment and field offsets as the C compiler reports for
//! `libobs/media-io/video-frame.h`.

use obs_c_oracle as _; // links the test bmalloc/bfree (oracle/test_bmem.c)

use core::mem::{align_of, offset_of, size_of};

use obs_c_oracle::video_frame::{
    oracle_video_frame_align, oracle_video_frame_offset_data, oracle_video_frame_offset_linesize,
    oracle_video_frame_size,
};
use obs_media_io::ffi::video_frame::video_frame;

#[test]
fn video_frame_layout_matches_c() {
    // SAFETY: layout getters take no inputs.
    unsafe {
        assert_eq!(size_of::<video_frame>(), oracle_video_frame_size());
        assert_eq!(align_of::<video_frame>(), oracle_video_frame_align());
        assert_eq!(
            offset_of!(video_frame, data),
            oracle_video_frame_offset_data()
        );
        assert_eq!(
            offset_of!(video_frame, linesize),
            oracle_video_frame_offset_linesize()
        );
    }
}
