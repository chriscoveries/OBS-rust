//! Rust port of `libobs/obs-properties.c`.
//!
//! The safe core is [`props`]. The C ABI shims live in [`ffi`].

#![deny(unsafe_code)]

// The lib test harness has no tests of its own, but it still links the
// `no_mangle` shims. Windows rust-lld keeps those shims, so the symbols
// they call have to come from a crate the harness actually references.
#[cfg(test)]
use obs_c_oracle as _; // blog, obs_obj_get_data
#[cfg(test)]
use obs_util as _; // bmem

pub mod props;

#[allow(unsafe_code)]
pub mod ffi;
