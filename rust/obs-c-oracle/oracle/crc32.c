/* Test-only oracle: the original libobs/util/crc32.c, unmodified, with its
 * global symbol renamed to oracle_* so it can link next to the Rust
 * implementation. */
#define calc_crc32 oracle_calc_crc32

#include "util/crc32.c.h"
