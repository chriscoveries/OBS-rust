//! `struct bounds` matches the layout of `libobs/graphics/bounds.h`.

use core::mem::{align_of, offset_of, size_of};

use obs_c_oracle::graphics_math as c;
use obs_graphics::ffi::bounds::bounds;

#[test]
fn bounds_layout_matches_c_header() {
    // SAFETY: the oracle layout functions take no arguments and only return constants.
    unsafe {
        assert_eq!(size_of::<bounds>(), c::oracle_bounds_size());
        assert_eq!(align_of::<bounds>(), c::oracle_bounds_align());
        assert_eq!(offset_of!(bounds, min), c::oracle_bounds_offset_min());
        assert_eq!(offset_of!(bounds, max), c::oracle_bounds_offset_max());
    }
}
