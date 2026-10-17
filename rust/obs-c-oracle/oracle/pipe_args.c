/* Test-only oracle: the original libobs/util/pipe.c, unmodified, with every
 * global symbol renamed to oracle_* so it can link next to the Rust
 * implementation. dstr/bstrdup come from util/dstr.c and bmalloc/bfree from
 * test_bmem.c. */
#define os_process_args_create oracle_os_process_args_create
#define os_process_args_add_arg oracle_os_process_args_add_arg
#define os_process_args_add_argf oracle_os_process_args_add_argf
#define os_process_args_get_argc oracle_os_process_args_get_argc
#define os_process_args_get_argv oracle_os_process_args_get_argv
#define os_process_args_destroy oracle_os_process_args_destroy
/* dstr_vprintf is C-only and lives renamed in oracle/dstr_libc.c. */
#define dstr_vprintf oracle_dstr_vprintf

#include "util/pipe.c.h"
