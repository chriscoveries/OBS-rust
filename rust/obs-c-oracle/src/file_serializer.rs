use core::ffi::c_char;

pub use super::array_serializer::OracleSerializer;

unsafe extern "C" {
    pub fn oracle_file_input_serializer_init(s: *mut OracleSerializer, path: *const c_char)
    -> bool;
    pub fn oracle_file_input_serializer_free(s: *mut OracleSerializer);
    pub fn oracle_file_output_serializer_init(
        s: *mut OracleSerializer,
        path: *const c_char,
    ) -> bool;
    pub fn oracle_file_output_serializer_init_safe(
        s: *mut OracleSerializer,
        path: *const c_char,
        temp_ext: *const c_char,
    ) -> bool;
    pub fn oracle_file_output_serializer_free(s: *mut OracleSerializer);
}
