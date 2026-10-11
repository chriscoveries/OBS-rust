use core::ffi::c_void;

/// Independent declaration of `struct darray`. Intentionally not shared
/// with `obs-util`.
#[repr(C)]
#[derive(Debug)]
pub struct OracleDarray {
    pub array: *mut c_void,
    pub num: usize,
    pub capacity: usize,
}

unsafe extern "C" {
    pub fn oracle_darray_free(da: *mut OracleDarray);
    pub fn oracle_darray_reserve(es: usize, da: *mut OracleDarray, capacity: usize);
    pub fn oracle_darray_ensure_capacity(es: usize, da: *mut OracleDarray, new_size: usize);
    pub fn oracle_darray_resize(es: usize, da: *mut OracleDarray, size: usize);
    pub fn oracle_darray_clear(da: *mut OracleDarray);
    pub fn oracle_darray_push_back_array(
        es: usize,
        da: *mut OracleDarray,
        array: *const c_void,
        num: usize,
    ) -> usize;
    pub fn oracle_darray_erase(es: usize, da: *mut OracleDarray, idx: usize);
    pub fn oracle_darray_pop_back(es: usize, da: *mut OracleDarray);

    pub fn oracle_darray_size() -> usize;
    pub fn oracle_darray_align() -> usize;
    pub fn oracle_darray_offset_array() -> usize;
    pub fn oracle_darray_offset_num() -> usize;
    pub fn oracle_darray_offset_capacity() -> usize;
}
