//! `libobs/obs-nal.c`.

unsafe extern "C" {
    pub fn oracle_obs_nal_find_startcode(p: *const u8, end: *const u8) -> *const u8;
}
