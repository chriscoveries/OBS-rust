use core::ffi::c_int;

unsafe extern "C" {
    /// `color_space`, `range` and `format` are the C enums as the `int`s
    /// they are passed as. `range_min` and `range_max` may be null.
    pub fn oracle_video_format_get_parameters(
        color_space: c_int,
        range: c_int,
        matrix: *mut f32,
        range_min: *mut f32,
        range_max: *mut f32,
    ) -> bool;
    pub fn oracle_video_format_get_parameters_for_format(
        color_space: c_int,
        range: c_int,
        format: c_int,
        matrix: *mut f32,
        range_min: *mut f32,
        range_max: *mut f32,
    ) -> bool;

    pub fn oracle_video_colorspace_size() -> usize;
    pub fn oracle_video_colorspace_align() -> usize;
    pub fn oracle_video_colorspace_count() -> usize;
    pub fn oracle_video_colorspace_value(i: usize) -> c_int;

    pub fn oracle_video_range_type_size() -> usize;
    pub fn oracle_video_range_type_align() -> usize;
    pub fn oracle_video_range_type_count() -> usize;
    pub fn oracle_video_range_type_value(i: usize) -> c_int;
}
