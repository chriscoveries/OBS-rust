//! `oracle_*` build of `libobs/obs-hotkey-name-map.c`
//! (oracle/hotkey_name_map.c).

use core::ffi::{c_char, c_int};

unsafe extern "C" {
    pub fn oracle_obs_key_to_name(key: c_int) -> *const c_char;
    pub fn oracle_obs_key_from_name(name: *const c_char) -> c_int;
    /// No core (`false`, the linear-scan fallback) or a fresh zeroed core
    /// (`true`, the name map, built on first lookup).
    pub fn oracle_hotkey_name_map_set_core(on: bool);
}
