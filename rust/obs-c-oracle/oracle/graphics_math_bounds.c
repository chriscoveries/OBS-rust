/* Test-only oracle: the original libobs/graphics/bounds.c, unmodified, with
 * its global symbols renamed to oracle_* (graphics_math_names.h). */
#include "graphics_math_names.h"

#include "graphics/bounds.c.h"

#include <stddef.h>

/* Test-only exports called from Rust over FFI. */
size_t oracle_bounds_size(void);
size_t oracle_bounds_align(void);
size_t oracle_bounds_offset_min(void);
size_t oracle_bounds_offset_max(void);

/* Layout of struct bounds. Not part of bounds.c. */
size_t oracle_bounds_size(void)
{
	return sizeof(struct bounds);
}
size_t oracle_bounds_align(void)
{
	return _Alignof(struct bounds);
}
size_t oracle_bounds_offset_min(void)
{
	return offsetof(struct bounds, min);
}
size_t oracle_bounds_offset_max(void)
{
	return offsetof(struct bounds, max);
}
