//! Tier 2: `#[repr(C)] axisang` and `quat` match the layout the C compiler
//! produces for `libobs/graphics/axisang.h` and `quat.h`. `axisang` has no
//! `__m128` member, so unlike the vectors it is only 4-byte aligned.

use core::mem::{align_of, offset_of, size_of};

use obs_c_oracle::graphics_math as c;
use obs_graphics::ffi::axisang::axisang;
use obs_graphics::ffi::quat::quat;
use proptest as _;

#[test]
fn axisang_layout_matches_c_header() {
    // SAFETY: the oracle layout functions take no arguments and only return constants.
    unsafe {
        assert_eq!(size_of::<axisang>(), c::oracle_axisang_size());
        assert_eq!(align_of::<axisang>(), c::oracle_axisang_align());
        assert_eq!(offset_of!(axisang, x), c::oracle_axisang_offset_x());
        assert_eq!(offset_of!(axisang, y), c::oracle_axisang_offset_y());
        assert_eq!(offset_of!(axisang, z), c::oracle_axisang_offset_z());
        assert_eq!(offset_of!(axisang, w), c::oracle_axisang_offset_w());
        assert_eq!(c::oracle_axisang_offset_ptr(), 0);
    }
}

#[test]
fn quat_layout_matches_c_header() {
    // SAFETY: as above.
    unsafe {
        assert_eq!(size_of::<quat>(), c::oracle_quat_size());
        assert_eq!(align_of::<quat>(), c::oracle_quat_align());
        assert_eq!(offset_of!(quat, x), c::oracle_quat_offset_x());
        assert_eq!(offset_of!(quat, y), c::oracle_quat_offset_y());
        assert_eq!(offset_of!(quat, z), c::oracle_quat_offset_z());
        assert_eq!(offset_of!(quat, w), c::oracle_quat_offset_w());
        assert_eq!(c::oracle_quat_offset_m(), 0);
    }
}
