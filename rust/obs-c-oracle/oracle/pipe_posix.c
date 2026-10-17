/* Test-only oracle: the original libobs/util/pipe-posix.c, unmodified, with
 * every global symbol renamed to oracle_* so it can link next to the Rust
 * implementation. bmalloc/bfree come from test_bmem.c. The args getter it
 * calls internally is renamed too, so the oracle never touches the Rust
 * shim. Unix-only: this file needs <spawn.h>. */
/* -std=c11 hides POSIX declarations (fdopen) on glibc; libobs builds with
 * the GNU dialect, so ask for them explicitly. */
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#define os_process_args_get_argv oracle_os_process_args_get_argv
#define os_process_pipe_create oracle_os_process_pipe_create
#define os_process_pipe_create2 oracle_os_process_pipe_create2
#define os_process_pipe_destroy oracle_os_process_pipe_destroy
#define os_process_pipe_read oracle_os_process_pipe_read
#define os_process_pipe_read_err oracle_os_process_pipe_read_err
#define os_process_pipe_write oracle_os_process_pipe_write

#include "util/pipe-posix.c.h"
