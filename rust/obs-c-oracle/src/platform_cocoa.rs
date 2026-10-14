//! Oracle entry points for `libobs/util/platform-cocoa.m`.

use core::ffi::{c_char, c_int, c_void};

/// `struct dstr` as `cfstr_copy_dstr` writes it. Same layout as
/// `obs_util::ffi::dstr::dstr`.
#[repr(C)]
pub struct OracleDstr {
    pub array: *mut c_char,
    pub len: usize,
    pub capacity: usize,
}

/// `struct os_proc_memory_usage`.
#[repr(C)]
pub struct OracleProcMem {
    pub resident_size: u64,
    pub virtual_size: u64,
}

unsafe extern "C" {
    pub fn oracle_os_gettime_ns() -> u64;
    pub fn oracle_os_get_config_path(dst: *mut c_char, size: usize, name: *const c_char) -> c_int;
    pub fn oracle_os_get_config_path_ptr(name: *const c_char) -> *mut c_char;
    pub fn oracle_os_get_program_data_path(
        dst: *mut c_char,
        size: usize,
        name: *const c_char,
    ) -> c_int;
    pub fn oracle_os_get_program_data_path_ptr(name: *const c_char) -> *mut c_char;
    pub fn oracle_os_get_executable_path_ptr(name: *const c_char) -> *mut c_char;
    pub fn oracle_os_cpu_usage_info_start() -> *mut c_void;
    pub fn oracle_os_cpu_usage_info_query(info: *mut c_void) -> f64;
    pub fn oracle_os_cpu_usage_info_destroy(info: *mut c_void);
    pub fn oracle_os_request_high_performance(reason: *const c_char) -> *mut c_void;
    pub fn oracle_os_end_high_performance(token: *mut c_void);
    pub fn oracle_os_inhibit_sleep_create(reason: *const c_char) -> *mut c_void;
    pub fn oracle_os_inhibit_sleep_set_active(info: *mut c_void, active: bool) -> bool;
    pub fn oracle_os_inhibit_sleep_destroy(info: *mut c_void);
    pub fn oracle_os_get_emulation_status() -> bool;
    pub fn oracle_os_get_physical_cores() -> c_int;
    pub fn oracle_os_get_logical_cores() -> c_int;
    pub fn oracle_os_get_sys_free_size() -> u64;
    pub fn oracle_os_get_free_space(path: *const c_char) -> i64;
    pub fn oracle_os_get_free_disk_space(dir: *const c_char) -> u64;
    pub fn oracle_os_get_sys_total_size() -> u64;
    pub fn oracle_os_get_proc_memory_usage(usage: *mut OracleProcMem) -> bool;
    pub fn oracle_os_get_proc_resident_size() -> u64;
    pub fn oracle_os_get_proc_virtual_size() -> u64;
    pub fn oracle_cfstr_copy_cstr(cfstring: *const c_void, encoding: u32) -> *mut c_char;
    pub fn oracle_cfstr_copy_dstr(
        cfstring: *const c_void,
        encoding: u32,
        str: *mut OracleDstr,
    ) -> bool;
}
