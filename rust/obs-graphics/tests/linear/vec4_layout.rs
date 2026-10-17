//! Tier 2: `#[repr(C)] vec4` and `matrix4` match the layout the C compiler
//! produces for `libobs/graphics/vec4.h` and `matrix4.h`
//! (including the `float ptr[4]` and `__m128 m` union members).

use core::mem::{align_of, offset_of, size_of};

use obs_c_oracle::graphics_math as c;
use obs_graphics::ffi::matrix4::matrix4;
use obs_graphics::ffi::vec4::vec4;
use proptest as _;

#[test]
fn vec4_layout_matches_c_header() {
    // SAFETY: the oracle layout functions take no arguments and only return constants.
    unsafe {
        assert_eq!(size_of::<vec4>(), c::oracle_vec4_size());
        assert_eq!(align_of::<vec4>(), c::oracle_vec4_align());
        assert_eq!(offset_of!(vec4, x), c::oracle_vec4_offset_x());
        assert_eq!(offset_of!(vec4, y), c::oracle_vec4_offset_y());
        assert_eq!(offset_of!(vec4, z), c::oracle_vec4_offset_z());
        assert_eq!(offset_of!(vec4, w), c::oracle_vec4_offset_w());
        assert_eq!(c::oracle_vec4_offset_ptr(), 0);
        assert_eq!(c::oracle_vec4_offset_m(), 0);
    }
}

#[test]
fn matrix4_layout_matches_c_header() {
    // SAFETY: as above.
    unsafe {
        assert_eq!(size_of::<matrix4>(), c::oracle_matrix4_size());
        assert_eq!(align_of::<matrix4>(), c::oracle_matrix4_align());
        assert_eq!(offset_of!(matrix4, x), c::oracle_matrix4_offset_x());
        assert_eq!(offset_of!(matrix4, y), c::oracle_matrix4_offset_y());
        assert_eq!(offset_of!(matrix4, z), c::oracle_matrix4_offset_z());
        assert_eq!(offset_of!(matrix4, t), c::oracle_matrix4_offset_t());
    }
}
