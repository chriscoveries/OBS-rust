//! Rust port of `libobs/obs-data.c`.
//!
//! The safe core is [`data`]. The C ABI shims live in [`ffi`].

#![deny(unsafe_code)]

// The lib test harness has no tests of its own, but it still links the
// `no_mangle` shims. Windows rust-lld keeps those shims, so the symbols
// they call have to come from a crate the harness actually references.
#[cfg(test)]
use obs_c_oracle as _; // jansson, blog, and the os_quick_* file helpers
#[cfg(test)]
use obs_util as _; // bfree, os_file_exists, os_rename, os_safe_replace, dstr

pub mod data;

#[allow(unsafe_code)]
pub mod ffi;

pub use data::{Array, Data, Fps, FpsRead, Item, NumType, Quat, Type, Vec2, Vec3, Vec4};
