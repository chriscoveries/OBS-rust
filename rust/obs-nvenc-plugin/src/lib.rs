//! The staticlib plugins/obs-nvenc links: obs-nvenc's C ABI shim.
//!
//! A separate crate so obs-nvenc stays a plain rlib: an rlib + staticlib crate
//! broke its own test build on windows-msvc (#592).

#![forbid(unsafe_code)]

pub use obs_nvenc::ffi;
