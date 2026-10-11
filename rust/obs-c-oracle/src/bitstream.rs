use core::ffi::c_int;

/// Independent declaration of `struct bitstream_reader` for calling the
/// oracle. Intentionally not shared with `obs-util`.
#[repr(C)]
#[allow(non_snake_case)]
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct OracleReader {
    pub pos: u8,
    pub subPos: u8,
    pub buf: *mut u8,
    pub len: usize,
}

unsafe extern "C" {
    pub fn oracle_bitstream_reader_init(r: *mut OracleReader, data: *mut u8, len: usize);
    pub fn oracle_bitstream_reader_read_bits(r: *mut OracleReader, bits: c_int) -> u8;
    pub fn oracle_bitstream_reader_r8(r: *mut OracleReader) -> u8;
    pub fn oracle_bitstream_reader_r16(r: *mut OracleReader) -> u16;

    pub fn oracle_bitstream_reader_size() -> usize;
    pub fn oracle_bitstream_reader_align() -> usize;
    pub fn oracle_bitstream_reader_offset_pos() -> usize;
    pub fn oracle_bitstream_reader_offset_subPos() -> usize;
    pub fn oracle_bitstream_reader_offset_buf() -> usize;
    pub fn oracle_bitstream_reader_offset_len() -> usize;
}
