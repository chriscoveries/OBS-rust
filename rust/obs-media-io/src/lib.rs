//! Rust ports of `libobs/media-io`.
//!
//! Safe cores live at the crate root; the C ABI shims that replace the
//! original `EXPORT` symbols live in [`ffi`]. See
//! `docs/rust-port/testing-policy.md`.

#[cfg(test)]
use obs_c_oracle as _; // links the test bmalloc/bfree (oracle/test_bmem.c)

pub mod audio_io;
pub mod audio_math;
pub mod ffi;
pub mod format_conversion;
pub mod frame_rate;
pub mod video_fourcc;
pub mod video_frame;
pub mod video_io;
pub mod video_matrices;
