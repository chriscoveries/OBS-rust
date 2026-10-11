use core::ffi::c_char;

unsafe extern "C" {
    pub fn oracle_os_get_path_extension(path: *const c_char) -> *const c_char;
}
