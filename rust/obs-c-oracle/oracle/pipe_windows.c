/* Test-only oracle: the original libobs/util/pipe-windows.c, unmodified, with
 * every global symbol renamed to oracle_* so it can link next to the Rust
 * implementation. The args getter it calls and the os_process_pipe_create it
 * calls from create2 are renamed too, so the oracle never touches the Rust
 * shim. dstr and bmalloc/bfree come from the Rust ports, os_utf8_to_wcs_ptr
 * from platform_conv_host.c and blog from base-variadic.c. Windows-only. */
#define os_process_args_get_argv oracle_os_process_args_get_argv
#define os_process_pipe_create oracle_os_process_pipe_create
#define os_process_pipe_create2 oracle_os_process_pipe_create2
#define os_process_pipe_destroy oracle_os_process_pipe_destroy
#define os_process_pipe_read oracle_os_process_pipe_read
#define os_process_pipe_read_err oracle_os_process_pipe_read_err
#define os_process_pipe_write oracle_os_process_pipe_write

#include "util/pipe-windows.c.h"
