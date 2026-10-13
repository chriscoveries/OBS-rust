//! ISO C `<locale.h>` and `<stdlib.h>` calls behind
//! [`os_strtod`](crate::platform::os_strtod), against the same CRT
//! (UCRT or MinGW) as libobs.

use core::ffi::{CStr, c_char};

/// The leading member of the CRT's `struct lconv`. `decimal_point` comes
/// first in both the UCRT and MinGW `<locale.h>`; only a pointer to the
/// struct is ever held, so the trailing members need no declaration.
#[repr(C)]
struct LconvPrefix {
    decimal_point: *mut c_char,
}

// `libc` declares no `localeconv` for Windows.
unsafe extern "C" {
    fn localeconv() -> *mut LconvPrefix;
}

/// First byte of `localeconv()->decimal_point`, the byte C's `to_locale`
/// (`libobs/util/platform.c`) writes over the first `.`.
pub fn decimal_point() -> u8 {
    // SAFETY: `localeconv` returns a non-null pointer to the CRT's
    // `struct lconv`, whose first member `decimal_point` is a non-null,
    // NUL-terminated string (ISO C 7.11.2.1), so reading its first byte is in
    // bounds. A concurrent `setlocale` races here exactly as it does in C.
    unsafe { *(*localeconv()).decimal_point as u8 }
}

/// `strtod(s, NULL)`.
pub fn strtod(s: &CStr) -> f64 {
    // SAFETY: `s` is a valid NUL-terminated string for the whole call, and a
    // null `endptr` is allowed.
    unsafe { libc::strtod(s.as_ptr(), core::ptr::null_mut()) }
}
