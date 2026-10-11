//! `libobs/obs-av1.c`.

unsafe extern "C" {
    pub fn oracle_obs_av1_keyframe(data: *const u8, size: usize) -> bool;
    pub fn oracle_obs_extract_av1_headers(
        packet: *const u8,
        size: usize,
        new_packet_data: *mut *mut u8,
        new_packet_size: *mut usize,
        header_data: *mut *mut u8,
        header_size: *mut usize,
    );
    pub fn oracle_metadata_obu_itu_t35(
        itut_t35_buffer: *const u8,
        itut_bufsize: usize,
        out_buffer: *mut *mut u8,
        outbuf_size: *mut usize,
    );
    pub fn oracle_metadata_obu(
        source_buffer: *const u8,
        source_bufsize: usize,
        out_buffer: *mut *mut u8,
        outbuf_size: *mut usize,
        metadata_type: u8,
    );
}
