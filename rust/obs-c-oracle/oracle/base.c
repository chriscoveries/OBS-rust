/* Test-only oracle: the original libobs/util/base.c, unmodified, with its
 * global symbols renamed to oracle_* so it can link next to the Rust
 * implementation and the C variadic adapter. */

/* base.c pulls in threading.h, whose recursive-mutex helper needs the
 * POSIX/XSI pthread declarations that strict -std=c11 hides. */
#ifndef _WIN32
#define _GNU_SOURCE
#endif
#define base_get_log_handler oracle_base_get_log_handler
#define base_set_log_handler oracle_base_set_log_handler
#define base_set_crash_handler oracle_base_set_crash_handler
#define blogva oracle_blogva
#define blog oracle_blog
#define bcrash oracle_bcrash

#include "util/base.c.h"
