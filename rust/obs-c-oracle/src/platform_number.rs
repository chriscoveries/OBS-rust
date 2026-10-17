use core::ffi::{c_char, c_int};

unsafe extern "C" {
    pub fn oracle_os_strtod(str: *const c_char) -> f64;
    pub fn oracle_os_dtostr(value: f64, dst: *mut c_char, size: usize) -> c_int;
}
