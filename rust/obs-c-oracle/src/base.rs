use core::ffi::c_void;

unsafe extern "C" {
    pub fn oracle_base_get_log_handler(handler: *mut *mut c_void, param: *mut *mut c_void);
    pub fn oracle_base_set_log_handler(handler: *mut c_void, param: *mut c_void);
    pub fn oracle_base_set_crash_handler(handler: *mut c_void, param: *mut c_void);
}
