//! Unix (Linux, macOS) side of the [platform boundary](super).

pub mod event;
pub mod nix;
pub mod pipe;

pub use crate::ffi::native_sem::Sem;
pub use event::Event;
pub use pipe::Pipe;

use std::os::fd::IntoRawFd;
use std::os::unix::ffi::OsStrExt;
use std::path::PathBuf;

/// Byte paths are passed to `fopen(3)` as-is.
pub fn path_from_bytes(bytes: &[u8]) -> PathBuf {
    PathBuf::from(std::ffi::OsStr::from_bytes(bytes))
}

/// Close an owned file and report errors that `File::drop` discards.
#[allow(unsafe_code)] // The platform boundary closes the owned descriptor once.
pub fn close_file(file: std::fs::File) -> std::io::Result<()> {
    let fd = file.into_raw_fd();
    // SAFETY: `into_raw_fd` transfers the valid descriptor to this call.
    // Do not retry after an error: the descriptor may already be closed/reused.
    if unsafe { libc::close(fd) } == 0 {
        Ok(())
    } else {
        Err(std::io::Error::last_os_error())
    }
}
