//! `struct encoder_packet` from `libobs/obs-encoder.h` and its C layout.
use core::ffi::{c_int, c_void};

/// Independent declaration of `struct encoder_packet`.
#[repr(C)]
#[derive(Debug, Clone, Copy)]
pub struct OracleEncoderPacket {
    pub data: *mut u8,
    pub size: usize,
    pub pts: i64,
    pub dts: i64,
    pub timebase_num: i32,
    pub timebase_den: i32,
    pub type_: c_int,
    pub keyframe: bool,
    pub dts_usec: i64,
    pub sys_dts_usec: i64,
    pub priority: c_int,
    pub drop_priority: c_int,
    pub track_idx: usize,
    pub encoder: *mut c_void,
}

unsafe extern "C" {
    pub fn oracle_encoder_packet_size() -> usize;
    pub fn oracle_encoder_packet_align() -> usize;
    pub fn oracle_sizeof_long() -> usize;
    pub fn oracle_encoder_packet_offset_data() -> usize;
    pub fn oracle_encoder_packet_offset_size() -> usize;
    pub fn oracle_encoder_packet_offset_pts() -> usize;
    pub fn oracle_encoder_packet_offset_dts() -> usize;
    pub fn oracle_encoder_packet_offset_timebase_num() -> usize;
    pub fn oracle_encoder_packet_offset_timebase_den() -> usize;
    pub fn oracle_encoder_packet_offset_type() -> usize;
    pub fn oracle_encoder_packet_offset_keyframe() -> usize;
    pub fn oracle_encoder_packet_offset_dts_usec() -> usize;
    pub fn oracle_encoder_packet_offset_sys_dts_usec() -> usize;
    pub fn oracle_encoder_packet_offset_priority() -> usize;
    pub fn oracle_encoder_packet_offset_drop_priority() -> usize;
    pub fn oracle_encoder_packet_offset_track_idx() -> usize;
    pub fn oracle_encoder_packet_offset_encoder() -> usize;
}
