//! Tier 2: `#[repr(C)] bitstream_reader` matches the layout the C compiler
//! produces for `libobs/util/bitstream.h`.

use core::mem::{align_of, offset_of, size_of};

use obs_c_oracle::bitstream as c;
use obs_util::ffi::bitstream::bitstream_reader;

#[test]
fn bitstream_reader_layout_matches_c_header() {
    // SAFETY: the oracle layout functions take no arguments and only return constants.
    unsafe {
        assert_eq!(
            size_of::<bitstream_reader>(),
            c::oracle_bitstream_reader_size()
        );
        assert_eq!(
            align_of::<bitstream_reader>(),
            c::oracle_bitstream_reader_align()
        );
        assert_eq!(
            offset_of!(bitstream_reader, pos),
            c::oracle_bitstream_reader_offset_pos()
        );
        assert_eq!(
            offset_of!(bitstream_reader, subPos),
            c::oracle_bitstream_reader_offset_subPos()
        );
        assert_eq!(
            offset_of!(bitstream_reader, buf),
            c::oracle_bitstream_reader_offset_buf()
        );
        assert_eq!(
            offset_of!(bitstream_reader, len),
            c::oracle_bitstream_reader_offset_len()
        );
    }
}
