//! Tier 2: `#[repr(C)] vec2` matches the layout the C compiler produces for
//! `libobs/graphics/vec2.h` (including the `float ptr[2]` union member).

use core::mem::{align_of, offset_of, size_of};

use obs_c_oracle::vec2 as c;
use obs_graphics::ffi::vec2::vec2;
use proptest as _;

#[test]
fn vec2_layout_matches_c_header() {
    // SAFETY: the oracle layout functions take no arguments and only return constants.
    unsafe {
        assert_eq!(size_of::<vec2>(), c::oracle_vec2_size());
        assert_eq!(align_of::<vec2>(), c::oracle_vec2_align());
        assert_eq!(offset_of!(vec2, x), c::oracle_vec2_offset_x());
        assert_eq!(offset_of!(vec2, y), c::oracle_vec2_offset_y());
        assert_eq!(c::oracle_vec2_offset_ptr(), 0);
    }
}
