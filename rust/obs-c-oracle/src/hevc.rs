//! `libobs/obs-hevc.c`, calling the oracle copies of obs-nal.c and
//! array-serializer.c.
use core::ffi::c_int;

pub use super::encoder_packet::OracleEncoderPacket;

unsafe extern "C" {
    pub fn oracle_obs_hevc_keyframe(data: *const u8, size: usize) -> bool;
    pub fn oracle_obs_parse_hevc_packet(
        hevc_packet: *mut OracleEncoderPacket,
        src: *const OracleEncoderPacket,
    );
    pub fn oracle_obs_parse_hevc_packet_priority(packet: *const OracleEncoderPacket) -> c_int;
    #[allow(clippy::too_many_arguments)]
    pub fn oracle_obs_extract_hevc_headers(
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
