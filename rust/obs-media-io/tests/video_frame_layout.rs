//! Tier 2: the `#[repr(C)]` mirror of `struct video_frame`
//! (`libobs/media-io/video-frame.h`) matches the layout the C compiler
//! produces.

use core::mem::{align_of, offset_of, size_of};

use obs_c_oracle::video_frame as c;
use obs_media_io::ffi::video_frame::video_frame;
use obs_media_io::video_io::MAX_AV_PLANES;
use proptest as _;

#[test]
fn video_frame_layout_matches_c_header() {
    // SAFETY: the oracle layout functions take no arguments and only return constants.
    unsafe {
        assert_eq!(size_of::<video_frame>(), c::oracle_video_frame_size());
        assert_eq!(align_of::<video_frame>(), c::oracle_video_frame_align());
        assert_eq!(MAX_AV_PLANES, c::oracle_video_frame_planes());
        assert_eq!(MAX_AV_PLANES, c::oracle_video_frame_linesize_planes());
        assert_eq!(
            offset_of!(video_frame, data),
            c::oracle_video_frame_offset_data()
        );
        assert_eq!(
            offset_of!(video_frame, linesize),
            c::oracle_video_frame_offset_linesize()
        );
    }
}
