//! ISO C `<locale.h>` and `<stdlib.h>` calls behind
//! [`os_strtod`](crate::platform::os_strtod).

#![allow(unsafe_code)]

use std::ffi::CStr;

/// First byte of `localeconv()->decimal_point`, the byte C's `to_locale`
/// (`libobs/util/platform.c`) writes over the first `.`.
pub fn decimal_point() -> u8 {
    // SAFETY: `localeconv` returns a non-null pointer to the C library's
    // `struct lconv`, whose `decimal_point` is a non-null, NUL-terminated
    // string (ISO C 7.11.2.1), so reading its first byte is in bounds. A
    // concurrent `setlocale` races here exactly as it does in C.
    unsafe { *(*libc::localeconv()).decimal_point as u8 }
}

/// `strtod(s, NULL)`.
pub fn strtod(s: &CStr) -> f64 {
    // SAFETY: `s` is a valid NUL-terminated string for the whole call, and a
    // null `endptr` is allowed.
    unsafe { libc::strtod(s.as_ptr(), std::ptr::null_mut()) }
}
