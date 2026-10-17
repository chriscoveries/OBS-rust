/* Test-only oracle: the original libobs/graphics/vec2.c, unmodified, with its
 * global symbols renamed to oracle_* so it can link next to the Rust
 * implementation, plus the struct layout as the C compiler sees it. */
#include <stddef.h>

#define vec2_abs oracle_vec2_abs
#define vec2_floor oracle_vec2_floor
#define vec2_ceil oracle_vec2_ceil
#define vec2_close oracle_vec2_close
#define vec2_norm oracle_vec2_norm

#include "graphics/vec2.c.h"

/* Test-only exports called from Rust over FFI. */
size_t oracle_vec2_size(void);
size_t oracle_vec2_align(void);
size_t oracle_vec2_offset_x(void);
size_t oracle_vec2_offset_y(void);
size_t oracle_vec2_offset_ptr(void);

/* Layout of struct vec2. */
size_t oracle_vec2_size(void)
{
	return sizeof(struct vec2);
}
size_t oracle_vec2_align(void)
{
	return _Alignof(struct vec2);
}
size_t oracle_vec2_offset_x(void)
{
	return offsetof(struct vec2, x);
}
size_t oracle_vec2_offset_y(void)
{
	return offsetof(struct vec2, y);
}
size_t oracle_vec2_offset_ptr(void)
{
	return offsetof(struct vec2, ptr);
}
