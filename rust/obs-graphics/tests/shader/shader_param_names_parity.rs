//! Tier 3: the Rust shims answer exactly like the original
//! `libobs/graphics/shader-param-names.c`, compiled as `oracle_*` with the C
//! `dstr.c`.
//!
//! Null is compared only for `get_address_mode`, where C reads it as `""`;
//! the other two call `strcmp(NULL)` (see the shim docs).
//!
//! Reminder: the mutation check (break the core, see this file fail) is done
//! once per port, not on every change.

use std::ffi::{CString, c_char, c_int};

use obs_c_oracle::shader_param_names as c;
use obs_graphics::ffi::shader_param_names as rs;
use obs_util as _; // bmem for the C dstr.c
use proptest::prelude::*;

type Lookup = unsafe extern "C" fn(*const c_char) -> c_int;

const PAIRS: [(Lookup, Lookup, &str); 3] = [
    (
        c::oracle_get_shader_param_type,
        rs::get_shader_param_type,
        "get_shader_param_type",
    ),
    (
        c::oracle_get_sample_filter,
        rs::get_sample_filter,
        "get_sample_filter",
    ),
    (
        c::oracle_get_address_mode,
        rs::get_address_mode,
        "get_address_mode",
    ),
];

/// Every name either side knows, in its spelling and in other cases.
const KNOWN: &[&str] = &[
    "float",
    "float2",
    "float3",
    "float4",
    "int2",
    "int3",
    "int4",
    "texture",
    "texture2d",
    "texture3d",
    "texture_cube",
    "textur",
    "float4x4",
    "bool",
    "int",
    "string",
    "Anisotropy",
    "Point",
    "Linear",
    "MIN_MAG_MIP_POINT",
    "MIN_MAG_MIP_LINEAR",
    "MIN_MAG_POINT_MIP_LINEAR",
    "MIN_POINT_MAG_LINEAR_MIP_POINT",
    "MIN_POINT_MAG_MIP_LINEAR",
    "MIN_LINEAR_MAG_MIP_POINT",
    "MIN_LINEAR_MAG_POINT_MIP_LINEAR",
    "MIN_MAG_LINEAR_MIP_POINT",
    "Wrap",
    "Repeat",
    "Clamp",
    "None",
    "Mirror",
    "Border",
    "MirrorOnce",
    "",
];

fn same(name: &[u8]) {
    let s = CString::new(name).unwrap();
    for (cf, rf, what) in PAIRS {
        // SAFETY: `s` is a valid NUL-terminated string.
        let (cv, rv) = unsafe { (cf(s.as_ptr()), rf(s.as_ptr())) };
        assert_eq!(cv, rv, "{what}({s:?})");
    }
}

#[test]
fn known_names_in_every_case() {
    for n in KNOWN {
        same(n.as_bytes());
        same(n.to_ascii_lowercase().as_bytes());
        same(n.to_ascii_uppercase().as_bytes());
        same(format!("{n} ").as_bytes());
        same(format!("{n}x").as_bytes());
    }
}

#[test]
fn null_address_mode() {
    // SAFETY: C's astrcmpi reads NULL as "".
    let (cv, rv) = unsafe {
        (
            c::oracle_get_address_mode(std::ptr::null()),
            rs::get_address_mode(std::ptr::null()),
        )
    };
    assert_eq!(cv, rv);
}

proptest! {
    #[test]
    fn random_names(name in "[A-Za-z0-9_ ]{0,34}") {
        same(name.as_bytes());
    }

    #[test]
    fn random_bytes(name in proptest::collection::vec(1u8..=255, 0..16)) {
        same(&name);
    }
}
