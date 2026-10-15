//! Static C ABI adapter for the safe `obs-preflight` engine crate.
//!
//! The ABI implementation lives with the engine at
//! `rust/obs-preflight/src/ffi`; this tiny target compiles that module into a
//! static library without changing the engine's normal Rust `rlib` target.

#![deny(unsafe_code)]

pub use obs_preflight::{
    DEFAULT_WINDOW_DURATION_MS, StatSample, Verdict, WindowResult, judge_window, roll_up_windows,
};

#[allow(unsafe_code)] // Isolated C ABI boundary.
#[path = "../../obs-preflight/src/ffi/mod.rs"]
mod ffi;
