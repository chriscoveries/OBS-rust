use core::ffi::c_int;

unsafe extern "C" {
    /// Returns `enum video_format` as the C `int` it is passed as.
    pub fn oracle_video_format_from_fourcc(fourcc: u32) -> c_int;

    pub fn oracle_video_format_size() -> usize;
    pub fn oracle_video_format_align() -> usize;
    pub fn oracle_video_format_count() -> usize;
    pub fn oracle_video_format_value(i: usize) -> c_int;
}
