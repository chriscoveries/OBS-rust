//! Oracle entry points for `libobs/util/platform-windows.c`.

use core::ffi::{c_char, c_int, c_void};

#[repr(C)]
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct OracleWinVer {
    pub major: c_int,
    pub minor: c_int,
    pub build: c_int,
    pub revis: c_int,
}

#[repr(C)]
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct OracleRegDword {
    pub status: i32,
    pub size: u32,
    pub return_value: u32,
}

#[repr(C)]
pub struct OracleDirent {
    pub d_name: [c_char; 256],
    pub directory: bool,
}

#[repr(C)]
pub struct OracleGlobEnt {
    pub path: *mut c_char,
    pub directory: bool,
}

#[repr(C)]
pub struct OracleGlob {
    pub gl_pathc: usize,
    pub gl_pathv: *mut OracleGlobEnt,
}

#[repr(C)]
pub struct OracleProcMem {
    pub resident_size: u64,
    pub virtual_size: u64,
}

unsafe extern "C" {
    pub fn oracle_os_file_exists(path: *const c_char) -> bool;
    pub fn oracle_os_unlink(path: *const c_char) -> c_int;
    pub fn oracle_os_rmdir(path: *const c_char) -> c_int;
    pub fn oracle_os_mkdir(path: *const c_char) -> c_int;
    pub fn oracle_os_rename(old_path: *const c_char, new_path: *const c_char) -> c_int;
    pub fn oracle_os_safe_replace(
        target: *const c_char,
        from: *const c_char,
        backup: *const c_char,
    ) -> c_int;
    pub fn oracle_os_copyfile(file_in: *const c_char, file_out: *const c_char) -> c_int;
    pub fn oracle_os_opendir(path: *const c_char) -> *mut c_void;
    pub fn oracle_os_readdir(dir: *mut c_void) -> *mut OracleDirent;
    pub fn oracle_os_closedir(dir: *mut c_void);
    pub fn oracle_os_glob(
        pattern: *const c_char,
        flags: c_int,
        pglob: *mut *mut OracleGlob,
    ) -> c_int;
    pub fn oracle_os_globfree(pglob: *mut OracleGlob);
    pub fn oracle_os_get_abs_path(path: *const c_char, abspath: *mut c_char, size: usize) -> usize;
    pub fn oracle_os_getcwd(path: *mut c_char, size: usize) -> *mut c_char;
    pub fn oracle_os_chdir(path: *const c_char) -> c_int;
    pub fn oracle_os_sleepto_ns(time_target: u64) -> bool;
    pub fn oracle_os_sleepto_ns_fast(time_target: u64) -> bool;
    pub fn oracle_get_win_ver(info: *mut OracleWinVer);
    pub fn oracle_get_win_ver_int() -> u32;
    pub fn oracle_get_win_release_id() -> *const c_char;
    pub fn oracle_is_64_bit_windows() -> bool;
    pub fn oracle_is_arm64_windows() -> bool;
    pub fn oracle_os_get_emulation_status() -> bool;
    pub fn oracle_os_get_physical_cores() -> c_int;
    pub fn oracle_os_get_logical_cores() -> c_int;
    pub fn oracle_os_get_sys_total_size() -> u64;
    pub fn oracle_get_dll_ver(lib: *const u16, info: *mut OracleWinVer) -> bool;
    pub fn oracle_get_reg_dword(
        key: *mut c_void,
        sub_key: *const u16,
        value_name: *const u16,
        info: *mut OracleRegDword,
    );
    pub fn oracle_os_get_config_path(dst: *mut c_char, size: usize, name: *const c_char) -> c_int;
    pub fn oracle_os_get_config_path_ptr(name: *const c_char) -> *mut c_char;
    pub fn oracle_os_get_program_data_path(
        dst: *mut c_char,
        size: usize,
        name: *const c_char,
    ) -> c_int;
    pub fn oracle_os_get_executable_path_ptr(name: *const c_char) -> *mut c_char;
    pub fn oracle_os_generate_uuid() -> *mut c_char;
    pub fn oracle_os_dlopen(path: *const c_char) -> *mut c_void;
    pub fn oracle_os_dlsym(module: *mut c_void, func: *const c_char) -> *mut c_void;
    pub fn oracle_os_dlclose(module: *mut c_void);
    pub fn oracle_os_is_obs_plugin(path: *const c_char) -> bool;
    pub fn oracle_get_plugin_info(path: *const c_char, is_obs_plugin: *mut bool);
    pub fn oracle_os_request_high_performance(reason: *const c_char) -> *mut c_void;
    pub fn oracle_os_get_free_space(path: *const c_char) -> i64;
    pub fn oracle_os_get_free_disk_space(dir: *const c_char) -> u64;
    pub fn oracle_os_get_proc_memory_usage(usage: *mut OracleProcMem) -> bool;
    pub fn oracle_os_inhibit_sleep_create(reason: *const c_char) -> *mut c_void;
    pub fn oracle_os_inhibit_sleep_set_active(info: *mut c_void, active: bool) -> bool;
    pub fn oracle_os_inhibit_sleep_destroy(info: *mut c_void);

    pub fn oracle_os_dirent_size() -> usize;
    pub fn oracle_os_dirent_align() -> usize;
    pub fn oracle_os_dirent_offset_d_name() -> usize;
    pub fn oracle_os_dirent_offset_directory() -> usize;
    pub fn oracle_os_globent_size() -> usize;
    pub fn oracle_os_globent_align() -> usize;
    pub fn oracle_os_globent_offset_path() -> usize;
    pub fn oracle_os_globent_offset_directory() -> usize;
    pub fn oracle_os_glob_info_size() -> usize;
    pub fn oracle_os_glob_info_align() -> usize;
    pub fn oracle_os_glob_info_offset_gl_pathc() -> usize;
    pub fn oracle_os_glob_info_offset_gl_pathv() -> usize;
    pub fn oracle_os_proc_memory_usage_size() -> usize;
    pub fn oracle_os_proc_memory_usage_align() -> usize;
    pub fn oracle_os_proc_memory_usage_offset_resident_size() -> usize;
    pub fn oracle_os_proc_memory_usage_offset_virtual_size() -> usize;
    pub fn oracle_win_version_info_size() -> usize;
    pub fn oracle_win_version_info_align() -> usize;
    pub fn oracle_win_version_info_offset_major() -> usize;
    pub fn oracle_win_version_info_offset_minor() -> usize;
    pub fn oracle_win_version_info_offset_build() -> usize;
    pub fn oracle_win_version_info_offset_revis() -> usize;
    pub fn oracle_reg_dword_size() -> usize;
    pub fn oracle_reg_dword_align() -> usize;
    pub fn oracle_reg_dword_offset_status() -> usize;
    pub fn oracle_reg_dword_offset_size() -> usize;
    pub fn oracle_reg_dword_offset_return_value() -> usize;
    pub fn oracle_memory_status_ex_size() -> usize;
    pub fn oracle_process_memory_counters_size() -> usize;
    pub fn oracle_logical_processor_size() -> usize;
}
