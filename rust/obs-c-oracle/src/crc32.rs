unsafe extern "C" {
    pub fn oracle_calc_crc32(crc: u32, buf: *const core::ffi::c_void, size: usize) -> u32;
}
