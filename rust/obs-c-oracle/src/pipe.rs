use core::ffi::c_char;
#[cfg(unix)]
use core::ffi::c_int;

/// Opaque `struct os_process_args`. Only pointers cross the boundary.
pub enum OracleArgs {}

/// Opaque `struct os_process_pipe`. Only pointers cross the boundary.
pub enum OraclePipe {}

unsafe extern "C" {
    pub fn oracle_os_process_args_create(executable: *const c_char) -> *mut OracleArgs;
    pub fn oracle_os_process_args_add_arg(args: *mut OracleArgs, arg: *const c_char);
    pub fn oracle_os_process_args_add_argf(args: *mut OracleArgs, format: *const c_char, ...);
    pub fn oracle_os_process_args_get_argc(args: *const OracleArgs) -> usize;
    pub fn oracle_os_process_args_get_argv(args: *const OracleArgs) -> *mut *mut c_char;
    pub fn oracle_os_process_args_destroy(args: *mut OracleArgs);

    #[cfg(unix)]
    pub fn oracle_os_process_pipe_create(
        cmd_line: *const c_char,
        type_: *const c_char,
    ) -> *mut OraclePipe;
    #[cfg(unix)]
    pub fn oracle_os_process_pipe_create2(
        args: *const OracleArgs,
        type_: *const c_char,
    ) -> *mut OraclePipe;
    #[cfg(unix)]
    pub fn oracle_os_process_pipe_destroy(pp: *mut OraclePipe) -> c_int;
    #[cfg(unix)]
    pub fn oracle_os_process_pipe_read(pp: *mut OraclePipe, data: *mut u8, len: usize) -> usize;
    #[cfg(unix)]
    pub fn oracle_os_process_pipe_read_err(pp: *mut OraclePipe, data: *mut u8, len: usize)
    -> usize;
    #[cfg(unix)]
    pub fn oracle_os_process_pipe_write(pp: *mut OraclePipe, data: *const u8, len: usize) -> usize;
}
