/* Test-only oracle: the original libobs/graphics/axisang.c, unmodified, with
 * its global symbols renamed to oracle_* (graphics_math_names.h). */
#include "graphics_math_names.h"

#include "graphics/axisang.c.h"

#include <stddef.h>

/* Test-only exports called from Rust over FFI. */
size_t oracle_axisang_size(void);
size_t oracle_axisang_align(void);
size_t oracle_axisang_offset_x(void);
size_t oracle_axisang_offset_y(void);
size_t oracle_axisang_offset_z(void);
size_t oracle_axisang_offset_w(void);
size_t oracle_axisang_offset_ptr(void);
size_t oracle_quat_size(void);
size_t oracle_quat_align(void);
size_t oracle_quat_offset_x(void);
size_t oracle_quat_offset_y(void);
size_t oracle_quat_offset_z(void);
size_t oracle_quat_offset_w(void);
size_t oracle_quat_offset_m(void);

/* Layout of struct axisang and struct quat. */
size_t oracle_axisang_size(void)
{
	return sizeof(struct axisang);
}
size_t oracle_axisang_align(void)
{
	return _Alignof(struct axisang);
}
size_t oracle_axisang_offset_x(void)
{
	return offsetof(struct axisang, x);
}
size_t oracle_axisang_offset_y(void)
{
	return offsetof(struct axisang, y);
}
size_t oracle_axisang_offset_z(void)
{
	return offsetof(struct axisang, z);
}
size_t oracle_axisang_offset_w(void)
{
	return offsetof(struct axisang, w);
}
size_t oracle_axisang_offset_ptr(void)
{
	return offsetof(struct axisang, ptr);
}
size_t oracle_quat_size(void)
{
	return sizeof(struct quat);
}
size_t oracle_quat_align(void)
{
	return _Alignof(struct quat);
}
size_t oracle_quat_offset_x(void)
{
	return offsetof(struct quat, x);
}
size_t oracle_quat_offset_y(void)
{
	return offsetof(struct quat, y);
}
size_t oracle_quat_offset_z(void)
{
	return offsetof(struct quat, z);
}
size_t oracle_quat_offset_w(void)
{
	return offsetof(struct quat, w);
}
size_t oracle_quat_offset_m(void)
{
	return offsetof(struct quat, m);
}
