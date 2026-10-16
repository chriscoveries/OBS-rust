//! Tier 1: 1:1 port of `test/cmocka/test_hotkey_name_map.c`, through the C
//! ABI shims. The Rust port has one map for both of C's paths (no core and
//! after `obs_startup`), so the two C tests run the same checks here.

use std::ffi::{CStr, CString, c_char};

use obs_c_oracle as _; // blog
use obs_hotkey::ffi::hotkey_name_map::{obs_key_from_name, obs_key_to_name};
use obs_util as _; // base log handler

const OBS_KEY_NONE: i32 = 0;

/// The key list, in enum order, straight from the header the C uses.
fn key_names() -> Vec<String> {
    include_str!("../../../libobs/obs-hotkeys.h")
        .lines()
        .filter_map(|l| {
            let l = l.trim();
            l.strip_prefix("OBS_HOTKEY(")
                .or_else(|| l.strip_prefix("OBS_MOUSE_BUTTON("))
                .and_then(|r| r.strip_suffix(')'))
                .map(str::to_owned)
        })
        .collect()
}

fn to_name(key: i32) -> String {
    // SAFETY: the shim returns a static NUL-terminated string.
    unsafe { CStr::from_ptr(obs_key_to_name(key)) }
        .to_str()
        .unwrap()
        .to_owned()
}

fn from_name(name: &str) -> i32 {
    let c = CString::new(name).unwrap();
    // SAFETY: `c` is a valid NUL-terminated string.
    unsafe { obs_key_from_name(c.as_ptr()) }
}

fn check_names() {
    let names = key_names();
    let last = names.len() as i32; // OBS_KEY_LAST_VALUE

    // every key round-trips code -> name -> code
    for (i, n) in names.iter().enumerate() {
        let key = i as i32;
        let name = to_name(key);
        assert_eq!(&name, n);
        assert_eq!(from_name(&name), key);
    }

    let key_a = names.iter().position(|n| n == "OBS_KEY_A").unwrap() as i32;
    assert_eq!(from_name("OBS_KEY_A"), key_a);
    assert_eq!(from_name("OBS_KEY_NONE"), OBS_KEY_NONE);
    assert_eq!(to_name(OBS_KEY_NONE), "OBS_KEY_NONE");

    // unknown names are OBS_KEY_NONE; matching is exact and case sensitive
    assert_eq!(from_name(""), OBS_KEY_NONE);
    assert_eq!(from_name("OBS_KEY_"), OBS_KEY_NONE);
    assert_eq!(from_name("obs_key_a"), OBS_KEY_NONE);
    assert_eq!(from_name("OBS_KEY_A "), OBS_KEY_NONE);
    assert_eq!(from_name("OBS_KEY_LAST_VALUE"), OBS_KEY_NONE);

    // codes at and past the end log an error and give ""
    assert_eq!(to_name(last), "");
    assert_eq!(to_name(last + 1), "");
    assert_eq!(to_name(100_000), "");
}

#[test]
fn test_without_core() {
    check_names();
}

#[test]
fn test_with_core() {
    check_names();
    // the map is built once; a second pass reads the same map
    check_names();
}

/// Intentional difference: C's no-core fallback calls `strcmp(NULL)`.
#[test]
fn null_name_is_none() {
    // SAFETY: null is accepted by the shim.
    assert_eq!(
        unsafe { obs_key_from_name(std::ptr::null::<c_char>()) },
        OBS_KEY_NONE
    );
}
