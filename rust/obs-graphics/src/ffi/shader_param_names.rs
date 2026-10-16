//! C ABI for the lookups in `libobs/graphics/shader-parser.h`
//! (`libobs/graphics/shader-param-names.c`). The enums are C `int`s.
//!
//! Intentional difference from C: a null name gives the fallback value
//! (`UNKNOWN`, `LINEAR`); C calls `strcmp(NULL)` there. `get_address_mode`
//! uses only `astrcmpi`, which reads NULL as `""`, so its null case is the
//! same in both: `GS_ADDRESS_CLAMP`.

use core::ffi::{CStr, c_char, c_int};

use crate::shader_param_names::{
    address, address_mode, filter, param, sample_filter, shader_param_type,
};

/// The bytes of `s`, or `None` for null.
unsafe fn bytes<'a>(s: *const c_char) -> Option<&'a [u8]> {
    // SAFETY: the caller passes null or a NUL-terminated string.
    (!s.is_null()).then(|| unsafe { CStr::from_ptr(s) }.to_bytes())
}

/// # Safety
///
/// `type_` must be null or a valid NUL-terminated string.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn get_shader_param_type(type_: *const c_char) -> c_int {
    unsafe { bytes(type_) }.map_or(param::UNKNOWN, shader_param_type)
}

/// # Safety
///
/// `filter` must be null or a valid NUL-terminated string.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn get_sample_filter(filter: *const c_char) -> c_int {
    unsafe { bytes(filter) }.map_or(filter::LINEAR, sample_filter)
}

/// # Safety
///
/// `mode` must be null or a valid NUL-terminated string.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn get_address_mode(mode: *const c_char) -> c_int {
    unsafe { bytes(mode) }.map_or(address::CLAMP, address_mode)
}
