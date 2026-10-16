//! Tier 3: the Rust shims answer exactly like the original
//! `libobs/obs-hotkey-name-map.c` (compiled as `oracle_*`), in both of C's
//! modes: without a core (linear scan) and with one (the name map).
//!
//! Negative codes and null names are not compared: C has undefined
//! behavior there (see the shim docs).
//!
//! Reminder: the mutation check (break the core, see this file fail) is done
//! once per port, not on every change.

use std::ffi::{CStr, CString};
use std::sync::Mutex;

use obs_c_oracle::hotkey_name_map as c;
use obs_hotkey::ffi::hotkey_name_map as rs;
use obs_util as _; // base log handler
use proptest::prelude::*;

/// The oracle's core is a global; tests take turns.
static CORE: Mutex<()> = Mutex::new(());

fn with_core<R>(on: bool, f: impl FnOnce() -> R) -> R {
    let _g = CORE.lock().unwrap_or_else(|e| e.into_inner());
    // SAFETY: only these tests touch the oracle's core, under the lock.
    unsafe { c::oracle_hotkey_name_map_set_core(on) };
    let r = f();
    unsafe { c::oracle_hotkey_name_map_set_core(false) };
    r
}

fn names(key: i32) -> (Vec<u8>, Vec<u8>) {
    // SAFETY: both return static NUL-terminated strings for key >= 0.
    unsafe {
        (
            CStr::from_ptr(c::oracle_obs_key_to_name(key))
                .to_bytes()
                .to_vec(),
            CStr::from_ptr(rs::obs_key_to_name(key)).to_bytes().to_vec(),
        )
    }
}

fn keys(name: &CStr) -> (i32, i32) {
    // SAFETY: `name` is a valid NUL-terminated string.
    unsafe {
        (
            c::oracle_obs_key_from_name(name.as_ptr()),
            rs::obs_key_from_name(name.as_ptr()),
        )
    }
}

/// Every code up to well past the end, and every name, in both modes.
#[test]
fn every_key_matches() {
    for on in [false, true] {
        with_core(on, || {
            for key in 0..2000 {
                let (cn, rn) = names(key);
                assert_eq!(cn, rn, "obs_key_to_name({key}), core={on}");
                let name = CString::new(cn).unwrap();
                let (ck, rk) = keys(&name);
                assert_eq!(ck, rk, "obs_key_from_name({name:?}), core={on}");
            }
        });
    }
}

proptest! {
    #[test]
    fn names_match(name in "(OBS_KEY_)?[A-Za-z0-9_ ]{0,12}", on in any::<bool>()) {
        let name = CString::new(name).unwrap();
        let (ck, rk) = with_core(on, || keys(&name));
        prop_assert_eq!(ck, rk, "obs_key_from_name({:?}), core={}", name, on);
    }

    #[test]
    fn codes_match(key in 0i32..i32::MAX) {
        let (cn, rn) = names(key);
        prop_assert_eq!(cn, rn, "obs_key_to_name({})", key);
    }
}
