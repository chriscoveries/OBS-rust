use core::ffi::c_int;

/// Independent declaration of `struct vec2`. Intentionally not shared
/// with `obs-graphics`.
#[repr(C)]
#[derive(Debug, Clone, Copy, Default, PartialEq)]
pub struct OracleVec2 {
    pub x: f32,
    pub y: f32,
}

unsafe extern "C" {
    pub fn oracle_vec2_abs(dst: *mut OracleVec2, v: *const OracleVec2);
    pub fn oracle_vec2_floor(dst: *mut OracleVec2, v: *const OracleVec2);
    pub fn oracle_vec2_ceil(dst: *mut OracleVec2, v: *const OracleVec2);
    pub fn oracle_vec2_close(v1: *const OracleVec2, v2: *const OracleVec2, epsilon: f32) -> c_int;
    pub fn oracle_vec2_norm(dst: *mut OracleVec2, v: *const OracleVec2);

    pub fn oracle_vec2_size() -> usize;
    pub fn oracle_vec2_align() -> usize;
    pub fn oracle_vec2_offset_x() -> usize;
    pub fn oracle_vec2_offset_y() -> usize;
    pub fn oracle_vec2_offset_ptr() -> usize;
}
