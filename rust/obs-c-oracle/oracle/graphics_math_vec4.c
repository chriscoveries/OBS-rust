/* Test-only oracle: the original libobs/graphics/vec4.c, unmodified, with
 * its global symbols renamed to oracle_* (graphics_math_names.h). */
#include "graphics_math_names.h"

#include "graphics/vec4.c.h"

#include <stddef.h>

/* Test-only exports called from Rust over FFI. */
size_t oracle_vec3_size(void);
size_t oracle_vec3_align(void);
size_t oracle_vec4_size(void);
size_t oracle_vec4_align(void);
size_t oracle_vec4_offset_x(void);
size_t oracle_vec4_offset_y(void);
size_t oracle_vec4_offset_z(void);
size_t oracle_vec4_offset_w(void);
size_t oracle_vec4_offset_ptr(void);
size_t oracle_vec4_offset_m(void);
size_t oracle_matrix4_size(void);
size_t oracle_matrix4_align(void);
size_t oracle_matrix4_offset_x(void);
size_t oracle_matrix4_offset_y(void);
size_t oracle_matrix4_offset_z(void);
size_t oracle_matrix4_offset_t(void);

/* Layout of struct vec3, struct vec4 and struct matrix4. */
size_t oracle_vec3_size(void)
{
	return sizeof(struct vec3);
}
size_t oracle_vec3_align(void)
{
	return _Alignof(struct vec3);
}
size_t oracle_vec4_size(void)
{
	return sizeof(struct vec4);
}
size_t oracle_vec4_align(void)
{
	return _Alignof(struct vec4);
}
size_t oracle_vec4_offset_x(void)
{
	return offsetof(struct vec4, x);
}
size_t oracle_vec4_offset_y(void)
{
	return offsetof(struct vec4, y);
}
size_t oracle_vec4_offset_z(void)
{
	return offsetof(struct vec4, z);
}
size_t oracle_vec4_offset_w(void)
{
	return offsetof(struct vec4, w);
}
size_t oracle_vec4_offset_ptr(void)
{
	return offsetof(struct vec4, ptr);
}
size_t oracle_vec4_offset_m(void)
{
	return offsetof(struct vec4, m);
}
size_t oracle_matrix4_size(void)
{
	return sizeof(struct matrix4);
}
size_t oracle_matrix4_align(void)
{
	return _Alignof(struct matrix4);
}
size_t oracle_matrix4_offset_x(void)
{
	return offsetof(struct matrix4, x);
}
size_t oracle_matrix4_offset_y(void)
{
	return offsetof(struct matrix4, y);
}
size_t oracle_matrix4_offset_z(void)
{
	return offsetof(struct matrix4, z);
}
size_t oracle_matrix4_offset_t(void)
{
	return offsetof(struct matrix4, t);
}
