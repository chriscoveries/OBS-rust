use core::ffi::c_char;

pub use super::array_serializer::OracleSerializer;

unsafe extern "C" {
    pub fn oracle_buffered_file_serializer_init_defaults(
        s: *mut OracleSerializer,
        path: *const c_char,
    ) -> bool;
    pub fn oracle_buffered_file_serializer_init(
        s: *mut OracleSerializer,
        path: *const c_char,
        max_bufsize: usize,
        chunk_size: usize,
    ) -> bool;
    pub fn oracle_buffered_file_serializer_free(s: *mut OracleSerializer);
}
