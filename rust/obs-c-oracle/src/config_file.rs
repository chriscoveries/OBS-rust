use core::ffi::{c_char, c_int};

/// `struct config_data` is opaque; only its address matters.
#[repr(C)]
pub struct OracleConfig {
    _private: [u8; 0],
}

unsafe extern "C" {
    pub fn oracle_config_create(file: *const c_char) -> *mut OracleConfig;
    pub fn oracle_config_open(
        config: *mut *mut OracleConfig,
        file: *const c_char,
        open_type: c_int,
    ) -> c_int;
    pub fn oracle_config_open_string(config: *mut *mut OracleConfig, str_: *const c_char) -> c_int;
    pub fn oracle_config_open_defaults(config: *mut OracleConfig, file: *const c_char) -> c_int;
    pub fn oracle_config_save(config: *mut OracleConfig) -> c_int;
    pub fn oracle_config_save_safe(
        config: *mut OracleConfig,
        temp_ext: *const c_char,
        backup_ext: *const c_char,
    ) -> c_int;
    pub fn oracle_config_close(config: *mut OracleConfig);
    pub fn oracle_config_num_sections(config: *mut OracleConfig) -> usize;
    pub fn oracle_config_get_section(config: *mut OracleConfig, idx: usize) -> *const c_char;
    pub fn oracle_config_get_string(
        config: *mut OracleConfig,
        section: *const c_char,
        name: *const c_char,
    ) -> *const c_char;
    pub fn oracle_config_get_int(
        config: *mut OracleConfig,
        section: *const c_char,
        name: *const c_char,
    ) -> i64;
    pub fn oracle_config_get_uint(
        config: *mut OracleConfig,
        section: *const c_char,
        name: *const c_char,
    ) -> u64;
    pub fn oracle_config_get_bool(
        config: *mut OracleConfig,
        section: *const c_char,
        name: *const c_char,
    ) -> bool;
    pub fn oracle_config_get_double(
        config: *mut OracleConfig,
        section: *const c_char,
        name: *const c_char,
    ) -> f64;
    pub fn oracle_config_remove_value(
        config: *mut OracleConfig,
        section: *const c_char,
        name: *const c_char,
    ) -> bool;
    pub fn oracle_config_set_string(
        config: *mut OracleConfig,
        section: *const c_char,
        name: *const c_char,
        value: *const c_char,
    );
    pub fn oracle_config_set_int(
        config: *mut OracleConfig,
        section: *const c_char,
        name: *const c_char,
        value: i64,
    );
    pub fn oracle_config_set_uint(
        config: *mut OracleConfig,
        section: *const c_char,
        name: *const c_char,
        value: u64,
    );
    pub fn oracle_config_set_bool(
        config: *mut OracleConfig,
        section: *const c_char,
        name: *const c_char,
        value: bool,
    );
    pub fn oracle_config_set_double(
        config: *mut OracleConfig,
        section: *const c_char,
        name: *const c_char,
        value: f64,
    );
    pub fn oracle_config_set_default_string(
        config: *mut OracleConfig,
        section: *const c_char,
        name: *const c_char,
        value: *const c_char,
    );
    pub fn oracle_config_set_default_int(
        config: *mut OracleConfig,
        section: *const c_char,
        name: *const c_char,
        value: i64,
    );
    pub fn oracle_config_set_default_uint(
        config: *mut OracleConfig,
        section: *const c_char,
        name: *const c_char,
        value: u64,
    );
    pub fn oracle_config_set_default_bool(
        config: *mut OracleConfig,
        section: *const c_char,
        name: *const c_char,
        value: bool,
    );
    pub fn oracle_config_set_default_double(
        config: *mut OracleConfig,
        section: *const c_char,
        name: *const c_char,
        value: f64,
    );
    pub fn oracle_config_get_default_string(
        config: *mut OracleConfig,
        section: *const c_char,
        name: *const c_char,
    ) -> *const c_char;
    pub fn oracle_config_get_default_int(
        config: *mut OracleConfig,
        section: *const c_char,
        name: *const c_char,
    ) -> i64;
    pub fn oracle_config_get_default_uint(
        config: *mut OracleConfig,
        section: *const c_char,
        name: *const c_char,
    ) -> u64;
    pub fn oracle_config_get_default_bool(
        config: *mut OracleConfig,
        section: *const c_char,
        name: *const c_char,
    ) -> bool;
    pub fn oracle_config_get_default_double(
        config: *mut OracleConfig,
        section: *const c_char,
        name: *const c_char,
    ) -> f64;
    pub fn oracle_config_has_user_value(
        config: *mut OracleConfig,
        section: *const c_char,
        name: *const c_char,
    ) -> bool;
    pub fn oracle_config_has_default_value(
        config: *mut OracleConfig,
        section: *const c_char,
        name: *const c_char,
    ) -> bool;
}
