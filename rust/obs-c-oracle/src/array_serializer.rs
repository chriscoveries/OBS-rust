use core::ffi::{c_int, c_void};

/// Independent declaration of `struct serializer`.
#[repr(C)]
pub struct OracleSerializer {
    pub data: *mut c_void,
    pub read: Option<unsafe extern "C" fn(*mut c_void, *mut c_void, usize) -> usize>,
    pub write: Option<unsafe extern "C" fn(*mut c_void, *const c_void, usize) -> usize>,
    pub seek: Option<unsafe extern "C" fn(*mut c_void, i64, c_int) -> i64>,
    pub get_pos: Option<unsafe extern "C" fn(*mut c_void) -> i64>,
}

/// Independent declaration of `struct array_output_data`.
#[repr(C)]
pub struct OracleArrayOutputData {
    pub bytes: super::darray::OracleDarray,
    pub cur_pos: usize,
}

unsafe extern "C" {
    pub fn oracle_array_output_serializer_init(
        s: *mut OracleSerializer,
        data: *mut OracleArrayOutputData,
    );
    pub fn oracle_array_output_serializer_free(data: *mut OracleArrayOutputData);
    pub fn oracle_array_output_serializer_reset(data: *mut OracleArrayOutputData);

    pub fn oracle_serializer_size() -> usize;
    pub fn oracle_serializer_align() -> usize;
    pub fn oracle_serializer_offset_data() -> usize;
    pub fn oracle_serializer_offset_read() -> usize;
    pub fn oracle_serializer_offset_write() -> usize;
    pub fn oracle_serializer_offset_seek() -> usize;
    pub fn oracle_serializer_offset_get_pos() -> usize;

    pub fn oracle_array_output_data_size() -> usize;
    pub fn oracle_array_output_data_align() -> usize;
    pub fn oracle_array_output_data_offset_bytes() -> usize;
    pub fn oracle_array_output_data_offset_cur_pos() -> usize;
}
