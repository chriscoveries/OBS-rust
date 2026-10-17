/* Test-only oracle: the original libobs/util/platform-number.c, unmodified,
 * with its global symbols renamed to oracle_* so it can link next to the
 * Rust implementation. oracle/config_file.c uses the same renamed copies. */
#define os_strtod oracle_os_strtod
#define os_dtostr oracle_os_dtostr

#include "util/platform-number.c.h"
