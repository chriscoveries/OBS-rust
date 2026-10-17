/* Test-only oracle: the original libobs/util/path-extension.c, unmodified,
 * with its global symbol renamed to oracle_* so it can link next to the Rust
 * implementation. */
#define os_get_path_extension oracle_os_get_path_extension

#include "util/path-extension.c.h"
