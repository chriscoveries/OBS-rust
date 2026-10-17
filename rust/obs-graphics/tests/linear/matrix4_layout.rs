//! `struct matrix4` matches the layout of `libobs/graphics/matrix4.h`.

use core::mem::{align_of, offset_of, size_of};

use obs_c_oracle::graphics_math as c;
use obs_graphics::ffi::matrix4::matrix4;
use proptest as _;

#[test]
fn matrix4_layout_matches_c_header() {
    // SAFETY: the oracle layout functions take no arguments and only return constants.
    unsafe {
        assert_eq!(size_of::<matrix4>(), c::oracle_matrix4_size());
        assert_eq!(align_of::<matrix4>(), c::oracle_matrix4_align());
        assert_eq!(offset_of!(matrix4, x), c::oracle_matrix4_offset_x());
        assert_eq!(offset_of!(matrix4, y), c::oracle_matrix4_offset_y());
        assert_eq!(offset_of!(matrix4, z), c::oracle_matrix4_offset_z());
        assert_eq!(offset_of!(matrix4, t), c::oracle_matrix4_offset_t());
    }
}
