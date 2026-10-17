//! Rust port of `libobs/obs-hotkey-name-map.c`.
//!
//! The safe core is [`name_map`]. The C ABI shims live in [`ffi`].

#![deny(unsafe_code)]

// The lib test harness has no tests of its own, but it still links the
// `no_mangle` shims. Windows rust-lld keeps those shims, so the symbols
// they call have to come from a crate the harness actually references.
#[cfg(test)]
use obs_c_oracle as _; // blog
#[cfg(test)]
use obs_util as _; // base log handler

pub mod name_map;

#[allow(unsafe_code)]
pub mod ffi;
