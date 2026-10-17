//! `struct plane` matches the layout of `libobs/graphics/plane.h`.

use core::mem::{align_of, offset_of, size_of};

use obs_c_oracle::graphics_math as c;
use obs_graphics::ffi::plane::plane;
use proptest as _;

#[test]
fn plane_layout_matches_c_header() {
    // SAFETY: the oracle layout functions take no arguments and only return constants.
    unsafe {
        assert_eq!(size_of::<plane>(), c::oracle_plane_size());
        assert_eq!(align_of::<plane>(), c::oracle_plane_align());
        assert_eq!(offset_of!(plane, dir), c::oracle_plane_offset_dir());
        assert_eq!(offset_of!(plane, dist), c::oracle_plane_offset_dist());
    }
}
