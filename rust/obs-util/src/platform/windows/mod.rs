//! Windows side of the [platform boundary](super). [`pipe`], [`sync`] and
//! [`utf8`] call Win32 directly, declared against kernel32.lib.

pub mod pipe;
pub mod sync;
pub mod utf8;

pub use pipe::Pipe;
pub use sync::{Event, Sem, set_thread_name};

use std::os::windows::io::{IntoRawHandle, RawHandle};
use std::path::PathBuf;

#[link(name = "kernel32")]
#[allow(non_snake_case)]
unsafe extern "system" {
    fn CloseHandle(handle: RawHandle) -> i32;
}

/// libobs `os_fopen` converts with `utf8_to_wchar` (MultiByteToWideChar,
/// flags 0), which substitutes U+FFFD for invalid UTF-8 and then attempts
/// the substituted path.
pub fn path_from_bytes(bytes: &[u8]) -> PathBuf {
    PathBuf::from(String::from_utf8_lossy(bytes).into_owned())
}

/// Close an owned file and report errors that `File::drop` discards.
pub fn close_file(file: std::fs::File) -> std::io::Result<()> {
    let handle = file.into_raw_handle();
    // SAFETY: `into_raw_handle` transfers the valid handle to this call.
    if unsafe { CloseHandle(handle) } != 0 {
        Ok(())
    } else {
        Err(std::io::Error::last_os_error())
    }
}
