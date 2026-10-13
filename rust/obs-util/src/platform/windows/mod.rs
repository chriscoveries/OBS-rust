//! Windows side of the [platform boundary](super). [`pipe`], [`sync`] and
//! [`utf8`] call Win32 directly, declared against kernel32.lib.

pub mod crt;
pub mod pipe;
pub mod sync;
pub mod utf8;

pub use pipe::Pipe;
pub use sync::{Event, Sem, set_thread_name};

use std::path::PathBuf;

/// libobs `os_fopen` converts with `utf8_to_wchar` (MultiByteToWideChar,
/// flags 0), which substitutes U+FFFD for invalid UTF-8 and then attempts
/// the substituted path.
pub fn path_from_bytes(bytes: &[u8]) -> PathBuf {
    PathBuf::from(String::from_utf8_lossy(bytes).into_owned())
}
