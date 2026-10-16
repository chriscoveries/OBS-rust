//! Safe core for `libobs/obs-hotkey-name-map.c`: key code <-> key name.
//!
//! C keeps a uthash map in `obs->hotkeys`, built once with `pthread_once`,
//! and scans the key list when there is no core. Both find the first key
//! with the name, so this keeps one lazily built map for every caller.

use std::collections::HashMap;
use std::ffi::CStr;
use std::sync::OnceLock;

include!(concat!(env!("OUT_DIR"), "/keys.rs"));

/// `OBS_KEY_NONE`.
pub const KEY_NONE: i32 = 0;

/// `OBS_KEY_LAST_VALUE`: the number of keys.
pub fn key_count() -> usize {
    KEY_NAMES.len()
}

/// `obs_key_to_name`: the name of `key`, or `None` when it is not a key
/// (C logs and returns `""` then).
pub fn key_name(key: i32) -> Option<&'static CStr> {
    usize::try_from(key)
        .ok()
        .and_then(|i| KEY_NAMES.get(i).copied())
}

fn map() -> &'static HashMap<&'static CStr, i32> {
    static MAP: OnceLock<HashMap<&'static CStr, i32>> = OnceLock::new();
    MAP.get_or_init(|| {
        let mut m = HashMap::with_capacity(KEY_NAMES.len());
        for (i, name) in KEY_NAMES.iter().enumerate() {
            // obs_hotkey_name_map_insert keeps the first key with a name.
            m.entry(*name).or_insert(i as i32);
        }
        m
    })
}

/// `obs_key_from_name`: the first key named exactly `name`, or
/// `OBS_KEY_NONE`.
pub fn key_from_name(name: &CStr) -> i32 {
    map().get(name).copied().unwrap_or(KEY_NONE)
}
