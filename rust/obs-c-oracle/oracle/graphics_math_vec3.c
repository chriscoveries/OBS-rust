/* Test-only oracle: the original libobs/graphics/vec3.c, unmodified, with
 * its global symbols renamed to oracle_* (graphics_math_names.h). */
#include "graphics_math_names.h"

#include "graphics/vec3.c.h"

#include <stddef.h>

/* Test-only exports called from Rust over FFI. */
size_t oracle_vec3_offset_x(void);
size_t oracle_vec3_offset_y(void);
size_t oracle_vec3_offset_z(void);
size_t oracle_vec3_offset_w(void);
size_t oracle_vec3_offset_ptr(void);
size_t oracle_vec3_offset_m(void);
size_t oracle_plane_size(void);
size_t oracle_plane_align(void);
size_t oracle_plane_offset_dir(void);
size_t oracle_plane_offset_dist(void);
size_t oracle_matrix3_size(void);
size_t oracle_matrix3_align(void);
size_t oracle_matrix3_offset_x(void);
size_t oracle_matrix3_offset_y(void);
size_t oracle_matrix3_offset_z(void);
size_t oracle_matrix3_offset_t(void);

/* Layout of struct vec3, struct plane and struct matrix3. */
size_t oracle_vec3_offset_x(void)
{
	return offsetof(struct vec3, x);
}
size_t oracle_vec3_offset_y(void)
{
	return offsetof(struct vec3, y);
}
size_t oracle_vec3_offset_z(void)
{
	return offsetof(struct vec3, z);
}
size_t oracle_vec3_offset_w(void)
{
	return offsetof(struct vec3, w);
}
size_t oracle_vec3_offset_ptr(void)
{
	return offsetof(struct vec3, ptr);
}
size_t oracle_vec3_offset_m(void)
{
	return offsetof(struct vec3, m);
}
size_t oracle_plane_size(void)
{
	return sizeof(struct plane);
}
size_t oracle_plane_align(void)
{
	return _Alignof(struct plane);
}
size_t oracle_plane_offset_dir(void)
{
	return offsetof(struct plane, dir);
}
size_t oracle_plane_offset_dist(void)
{
	return offsetof(struct plane, dist);
}
size_t oracle_matrix3_size(void)
{
	return sizeof(struct matrix3);
}
size_t oracle_matrix3_align(void)
{
	return _Alignof(struct matrix3);
}
size_t oracle_matrix3_offset_x(void)
{
	return offsetof(struct matrix3, x);
}
size_t oracle_matrix3_offset_y(void)
{
	return offsetof(struct matrix3, y);
}
size_t oracle_matrix3_offset_z(void)
{
	return offsetof(struct matrix3, z);
}
size_t oracle_matrix3_offset_t(void)
{
	return offsetof(struct matrix3, t);
}
