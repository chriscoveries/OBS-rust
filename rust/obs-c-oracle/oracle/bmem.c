/* Test-only oracle: the original libobs/util/bmem.c, unmodified, with its
 * global symbols renamed to oracle_* so it can link next to the Rust
 * implementation. os_breakpoint/os_oom are renamed to the aborting copies in
 * oracle/platform_host.c - the plain names would resolve to the Rust port's
 * shims in a test binary - and bcrash comes from util/base-variadic.c. */
/* bmem.c pulls in util/threading.h, whose pthread_mutex_init_recursive needs
 * the POSIX/XSI extensions that a strict -std=c11 build hides on glibc. */
#if !defined(_WIN32) && !defined(_GNU_SOURCE)
#define _GNU_SOURCE
#endif

#define bmalloc oracle_bmalloc
#define os_breakpoint oracle_os_breakpoint
#define os_oom oracle_os_oom
#define brealloc oracle_brealloc
#define bfree oracle_bfree
#define bnum_allocs oracle_bnum_allocs
#define base_get_alignment oracle_base_get_alignment
#define bmemdup oracle_bmemdup

#include "util/bmem.c.h"
