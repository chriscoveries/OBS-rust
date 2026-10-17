//! C ABI shims for `os_strtod` and `os_dtostr` in `libobs/util/platform.h`.
//!
//! The functions only convert to and from the safe core in
//! [`crate::platform_number`].
//!
//! Intentional difference from C: the C functions swap `.` for the current
//! `LC_NUMERIC` decimal point around `strtod`/`snprintf`; the Rust core reads
//! and writes `.` directly. The results are identical for every input that
//! uses `.`; only a string that already contains the locale's own decimal
//! point (for example `"1,5"` under a `,` locale) reads differently: C
//! converts it, Rust stops at the `,`.

use core::ffi::{CStr, c_char, c_int};

use crate::platform_number::{dtostr, strtod};

/// Parses `str` as a `double`; 0 when nothing converts.
///
/// # Safety
///
/// `str` must point to a valid NUL-terminated string.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn os_strtod(str: *const c_char) -> f64 {
    // SAFETY: the caller guarantees a valid NUL-terminated string.
    strtod(unsafe { CStr::from_ptr(str) }.to_bytes())
}

/// Writes `value` into `dst` (at most `size` bytes, NUL included) and returns
/// the string length, or -1 when it does not fit.
///
/// # Safety
///
/// `dst` must be writable for `size` bytes.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn os_dtostr(value: f64, dst: *mut c_char, size: usize) -> c_int {
    let Some(text) = dtostr(value, size) else {
        return -1;
    };
    // SAFETY: `dtostr` returns `Some` only when the text and its NUL fit in
    // `size` bytes, and the caller guarantees `dst` is writable for `size`.
    unsafe {
        core::ptr::copy_nonoverlapping(text.as_ptr(), dst.cast::<u8>(), text.len());
        *dst.add(text.len()) = 0;
    }
    // A buffer that holds the text is far smaller than `c_int::MAX`.
    c_int::try_from(text.len()).unwrap_or(-1)
}
