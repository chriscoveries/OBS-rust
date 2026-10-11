use core::ffi::{c_int, c_void};

unsafe extern "C" {
    /// `format` is the C enum as the `int` it is passed as. `linesize` and
    /// `heights` must point to `MAX_AV_PLANES` zeroed `u32`s.
    pub fn oracle_video_frame_get_linesizes(linesize: *mut u32, format: c_int, width: u32);
    pub fn oracle_video_frame_get_plane_heights(heights: *mut u32, format: c_int, height: u32);

    /// `frame` points to a `struct video_frame`; may be null.
    pub fn oracle_video_frame_init(frame: *mut c_void, format: c_int, width: u32, height: u32);
    pub fn oracle_video_frame_copy(dst: *mut c_void, src: *const c_void, format: c_int, cy: u32);

    /// The `static inline` helpers in `video-frame.h`.
    pub fn oracle_video_frame_free(frame: *mut c_void);
    pub fn oracle_video_frame_create(format: c_int, width: u32, height: u32) -> *mut c_void;
    pub fn oracle_video_frame_destroy(frame: *mut c_void);

    pub fn oracle_video_frame_size() -> usize;
    pub fn oracle_video_frame_align() -> usize;
    pub fn oracle_video_frame_planes() -> usize;
    pub fn oracle_video_frame_linesize_planes() -> usize;
    pub fn oracle_video_frame_offset_data() -> usize;
    pub fn oracle_video_frame_offset_linesize() -> usize;
}
