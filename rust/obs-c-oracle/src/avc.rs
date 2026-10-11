//! `libobs/obs-avc.c`, calling the oracle copies of obs-nal.c,
//! array-serializer.c and bitstream.c.
use core::ffi::c_int;

pub use super::encoder_packet::OracleEncoderPacket;

unsafe extern "C" {
    pub fn oracle_obs_avc_keyframe(data: *const u8, size: usize) -> bool;
    pub fn oracle_obs_avc_find_startcode(p: *const u8, end: *const u8) -> *const u8;
    pub fn oracle_obs_parse_avc_packet(
        avc_packet: *mut OracleEncoderPacket,
        src: *const OracleEncoderPacket,
    );
    pub fn oracle_obs_parse_avc_packet_priority(packet: *const OracleEncoderPacket) -> c_int;
    pub fn oracle_obs_parse_avc_header(header: *mut *mut u8, data: *const u8, size: usize)
    -> usize;
    #[allow(clippy::too_many_arguments)]
    pub fn oracle_obs_extract_avc_headers(
        packet: *const u8,
        size: usize,
        new_packet_data: *mut *mut u8,
        new_packet_size: *mut usize,
        header_data: *mut *mut u8,
        header_size: *mut usize,
        sei_data: *mut *mut u8,
        sei_size: *mut usize,
    );
}
