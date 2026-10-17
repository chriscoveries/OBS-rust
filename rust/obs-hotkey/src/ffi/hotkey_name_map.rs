//! C ABI for the key name functions in `libobs/obs-hotkey.h`, and the
//! internal `obs_hotkey_name_map_free` from `obs-internal.h`.
//!
//! `obs_key_t` is a C enum, passed as `int`.
//!
//! Intentional differences from C, both where C has undefined behavior:
//! - a null name returns `OBS_KEY_NONE` (C's no-core fallback calls
//!   `strcmp(NULL)`);
//! - a negative key logs and returns `""` like a key past the end (C indexes
//!   before its array).

use std::ffi::{c_char, c_int};

use crate::name_map::{KEY_NONE, key_from_name, key_name};

const LOG_ERROR: c_int = 100;

unsafe extern "C" {
    fn blog(level: c_int, fmt: *const c_char, ...);
}

/// The name of `key`, or `""` (with an error logged) for an unknown code.
#[unsafe(no_mangle)]
pub extern "C" fn obs_key_to_name(key: c_int) -> *const c_char {
    match key_name(key) {
        Some(name) => name.as_ptr(),
        None => {
            // SAFETY: a literal format with one `%d`, matched by `key`.
            unsafe {
                blog(
                    LOG_ERROR,
                    c"obs-hotkey.c: queried unknown key with code %d".as_ptr(),
                    key,
                );
            }
            c"".as_ptr()
        }
    }
}

/// The key named `name`, or `OBS_KEY_NONE`.
///
/// # Safety
///
/// `name` must be null or a valid NUL-terminated string.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn obs_key_from_name(name: *const c_char) -> c_int {
    if name.is_null() {
        return KEY_NONE;
    }
    // SAFETY: the caller passes a valid NUL-terminated string.
    key_from_name(unsafe { std::ffi::CStr::from_ptr(name) })
}

/// Called by `obs_shutdown`. The map is a process-wide static that is built
/// once and never freed, so there is nothing to do.
#[unsafe(no_mangle)]
pub extern "C" fn obs_hotkey_name_map_free() {}
