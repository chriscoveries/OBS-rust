/* Test-only oracle: libobs/util/util_uint64.h is header-only (static
 * inline), so this file exposes a non-inline oracle_* wrapper that calls
 * the real header function. */

/* util_uint64.h uses uint64_t without including <stdint.h>. */
#include "util/c99defs.h"
#include "util/util_uint64.h"

uint64_t oracle_util_mul_div64(uint64_t num, uint64_t mul, uint64_t div)
{
	return util_mul_div64(num, mul, div);
}
