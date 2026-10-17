//! Static C ABI adapter for the safe `obs-preflight` engine crate.
//!
//! The ABI implementation lives with the engine at
//! `rust/obs-preflight/src/ffi`; this tiny target compiles that module into a
//! static library without changing the engine's normal Rust `rlib` target.
//! Sharing this module keeps the engine API and standalone C ABI on one
//! implementation while their crate types stay separate.

#![deny(unsafe_code)]

pub use obs_preflight::{
    BakeoffPlanner, CandidateMode, CandidatePlan, DEFAULT_WINDOW_DURATION_MS, StatSample, Verdict,
    WindowResult, expand_candidate_matrix, judge_window, roll_up_windows,
};

#[allow(unsafe_code)] // Isolated C ABI boundary.
#[path = "../../obs-preflight/src/ffi/mod.rs"]
mod ffi;
