//! plugins/obs-nvenc loader oracle (`oracle/obs_nvenc_loader.c`): the
//! original `load_cuda_lib`, `load_nvenc_lib` and `cuda_functions[]`, with a
//! recording `os_dlopen` that always fails.

use core::ffi::c_char;

unsafe extern "C" {
    pub fn oracle_load_cuda_lib() -> bool;
    pub fn oracle_load_nvenc_lib() -> bool;
    pub fn oracle_nvenc_reset_opened();
    pub fn oracle_nvenc_num_opened_names() -> usize;
    pub fn oracle_nvenc_opened_name(idx: usize) -> *const c_char;
    pub fn oracle_num_cuda_funcs() -> usize;
    pub fn oracle_cuda_func_name(idx: usize) -> *const c_char;
}
