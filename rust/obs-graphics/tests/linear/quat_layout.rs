//! Tier 2: `#[repr(C)] quat` matches the layout the C compiler produces
//! for `libobs/graphics/quat.h`. The union with `__m128` is 16-byte aligned.

use core::mem::{align_of, offset_of, size_of};

use obs_c_oracle::graphics_math as c;
use obs_graphics::ffi::quat::quat;

#[test]
fn quat_layout_matches_c_header() {
    // SAFETY: the oracle layout functions take no arguments and only return constants.
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
