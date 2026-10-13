//! Unix (Linux, macOS) side of the [platform boundary](super).

pub mod crt;
pub mod event;
pub mod nix;
pub mod pipe;

pub use crate::ffi::native_sem::Sem;
pub use event::Event;
pub use pipe::Pipe;

use std::os::unix::ffi::OsStrExt;
use std::path::PathBuf;

/// Byte paths are passed to `fopen(3)` as-is.
pub fn path_from_bytes(bytes: &[u8]) -> PathBuf {
    PathBuf::from(std::ffi::OsStr::from_bytes(bytes))
}
