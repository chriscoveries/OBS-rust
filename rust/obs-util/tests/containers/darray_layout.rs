//! Tier 2: `#[repr(C)] darray` matches the layout the C compiler produces for
//! `libobs/util/darray.h`.

use core::mem::{align_of, offset_of, size_of};

use obs_c_oracle::darray as c;
use obs_util::ffi::darray::darray;

#[test]
fn darray_layout_matches_c_header() {
    // SAFETY: the oracle layout functions take no arguments and only return constants.
    unsafe {
        assert_eq!(size_of::<darray>(), c::oracle_darray_size());
        assert_eq!(align_of::<darray>(), c::oracle_darray_align());
        assert_eq!(offset_of!(darray, array), c::oracle_darray_offset_array());
        assert_eq!(offset_of!(darray, num), c::oracle_darray_offset_num());
        assert_eq!(
            offset_of!(darray, capacity),
            c::oracle_darray_offset_capacity()
        );
    }
}
