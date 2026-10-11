//! `struct matrix3` matches the layout of `libobs/graphics/matrix3.h`.

use core::mem::{align_of, offset_of, size_of};

use obs_c_oracle::graphics_math as c;
use obs_graphics::ffi::matrix3::matrix3;
use proptest as _;

#[test]
fn matrix3_layout_matches_c_header() {
    // SAFETY: the oracle layout functions take no arguments and only return constants.
    unsafe {
        assert_eq!(size_of::<matrix3>(), c::oracle_matrix3_size());
        assert_eq!(align_of::<matrix3>(), c::oracle_matrix3_align());
        assert_eq!(offset_of!(matrix3, x), c::oracle_matrix3_offset_x());
        assert_eq!(offset_of!(matrix3, y), c::oracle_matrix3_offset_y());
        assert_eq!(offset_of!(matrix3, z), c::oracle_matrix3_offset_z());
        assert_eq!(offset_of!(matrix3, t), c::oracle_matrix3_offset_t());
    }
}
