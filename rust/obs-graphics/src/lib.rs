//! Rust ports of `libobs/graphics`.
//!
//! Safe cores live at the crate root; the C ABI shims that replace the
//! original `EXPORT` symbols live in [`ffi`]. See
//! `docs/rust-port/testing-policy.md`.

#![deny(unsafe_code)]

#[cfg(test)]
use obs_c_oracle as _; // links the test bmalloc/bfree (oracle/test_bmem.c)

pub mod axisang;
pub mod bounds;
#[allow(unsafe_code)] // the C ABI shims
pub mod ffi;
pub mod math_extra;
pub mod matrix3;
pub mod matrix4;
pub mod plane;
pub mod quat;
pub mod shader_param_names;
pub mod vec2;
pub mod vec3;
pub mod vec4;
