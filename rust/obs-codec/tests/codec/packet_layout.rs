//! Tier 2: `#[repr(C)] encoder_packet` matches the layout the C compiler
//! produces for `struct encoder_packet` in `libobs/obs-encoder.h`.

use core::mem::{align_of, offset_of, size_of};

use obs_c_oracle::encoder_packet as c;
use obs_codec::ffi::packet::encoder_packet;
use proptest as _;

#[test]
fn encoder_packet_layout_matches_c_header() {
    type P = encoder_packet;
    // SAFETY: the oracle layout functions take no arguments and only return constants.
    unsafe {
        assert_eq!(size_of::<P>(), c::oracle_encoder_packet_size());
        assert_eq!(align_of::<P>(), c::oracle_encoder_packet_align());
        assert_eq!(offset_of!(P, data), c::oracle_encoder_packet_offset_data());
        assert_eq!(offset_of!(P, size), c::oracle_encoder_packet_offset_size());
        assert_eq!(offset_of!(P, pts), c::oracle_encoder_packet_offset_pts());
        assert_eq!(offset_of!(P, dts), c::oracle_encoder_packet_offset_dts());
        assert_eq!(
            offset_of!(P, timebase_num),
            c::oracle_encoder_packet_offset_timebase_num()
        );
        assert_eq!(
            offset_of!(P, timebase_den),
            c::oracle_encoder_packet_offset_timebase_den()
        );
        assert_eq!(offset_of!(P, type_), c::oracle_encoder_packet_offset_type());
        assert_eq!(
            offset_of!(P, keyframe),
            c::oracle_encoder_packet_offset_keyframe()
        );
        assert_eq!(
            offset_of!(P, dts_usec),
            c::oracle_encoder_packet_offset_dts_usec()
        );
        assert_eq!(
            offset_of!(P, sys_dts_usec),
            c::oracle_encoder_packet_offset_sys_dts_usec()
        );
        assert_eq!(
            offset_of!(P, priority),
            c::oracle_encoder_packet_offset_priority()
        );
        assert_eq!(
            offset_of!(P, drop_priority),
            c::oracle_encoder_packet_offset_drop_priority()
        );
        assert_eq!(
            offset_of!(P, track_idx),
            c::oracle_encoder_packet_offset_track_idx()
        );
        assert_eq!(
            offset_of!(P, encoder),
            c::oracle_encoder_packet_offset_encoder()
        );
    }
}
