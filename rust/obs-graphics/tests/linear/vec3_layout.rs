//! Tier 2: `#[repr(C)] vec3`, `plane` and `matrix3` match the layout the C
//! compiler produces for `libobs/graphics/vec3.h`, `plane.h` and
//! `matrix3.h` (including the `float ptr[4]` and `__m128 m` union members).

use core::mem::{align_of, offset_of, size_of};

use obs_c_oracle::graphics_math as c;
use obs_graphics::ffi::matrix3::matrix3;
use obs_graphics::ffi::plane::plane;
use obs_graphics::ffi::vec3::vec3;
use proptest as _;

#[test]
fn vec3_layout_matches_c_header() {
    // SAFETY: the oracle layout functions take no arguments and only return constants.
    unsafe {
        assert_eq!(size_of::<vec3>(), c::oracle_vec3_size());
        assert_eq!(align_of::<vec3>(), c::oracle_vec3_align());
        assert_eq!(offset_of!(vec3, x), c::oracle_vec3_offset_x());
        assert_eq!(offset_of!(vec3, y), c::oracle_vec3_offset_y());
        assert_eq!(offset_of!(vec3, z), c::oracle_vec3_offset_z());
        assert_eq!(offset_of!(vec3, w), c::oracle_vec3_offset_w());
        assert_eq!(c::oracle_vec3_offset_ptr(), 0);
        assert_eq!(c::oracle_vec3_offset_m(), 0);
    }
}

#[test]
fn plane_layout_matches_c_header() {
    // SAFETY: as above.
    unsafe {
        assert_eq!(size_of::<plane>(), c::oracle_plane_size());
        assert_eq!(align_of::<plane>(), c::oracle_plane_align());
        assert_eq!(offset_of!(plane, dir), c::oracle_plane_offset_dir());
        assert_eq!(offset_of!(plane, dist), c::oracle_plane_offset_dist());
    }
}

#[test]
fn matrix3_layout_matches_c_header() {
    // SAFETY: as above.
    unsafe {
        assert_eq!(size_of::<matrix3>(), c::oracle_matrix3_size());
        assert_eq!(align_of::<matrix3>(), c::oracle_matrix3_align());
        assert_eq!(offset_of!(matrix3, x), c::oracle_matrix3_offset_x());
        assert_eq!(offset_of!(matrix3, y), c::oracle_matrix3_offset_y());
        assert_eq!(offset_of!(matrix3, z), c::oracle_matrix3_offset_z());
        assert_eq!(offset_of!(matrix3, t), c::oracle_matrix3_offset_t());
    }
}
