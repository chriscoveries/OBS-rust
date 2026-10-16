//! `oracle_*` build of `libobs/graphics/shader-param-names.c`
//! (oracle/shader_param_names.c).

use core::ffi::{c_char, c_int};

unsafe extern "C" {
    pub fn oracle_get_shader_param_type(type_: *const c_char) -> c_int;
    pub fn oracle_get_sample_filter(filter: *const c_char) -> c_int;
    pub fn oracle_get_address_mode(mode: *const c_char) -> c_int;
}
