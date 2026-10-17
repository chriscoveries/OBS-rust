/* Test-only oracle: the original libobs/media-io/video-matrices.c,
 * unmodified, with its global symbols renamed to oracle_* so it can link
 * next to the Rust implementation, plus the layouts of enum video_colorspace
 * and enum video_range_type as the C compiler sees them. Its vec3_rotate
 * call resolves to the oracle vec3.c (graphics_math_names.h). */
#include <stddef.h>

#include "graphics_math_names.h"

#define video_format_get_parameters oracle_video_format_get_parameters
#define video_format_get_parameters_for_format oracle_video_format_get_parameters_for_format

/* format_info[] leaves its matrix tables to the lazy initializer, which
 * -Wextra reports as missing initializers. */
#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
#endif
#include "media-io/video-matrices.c.h"
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

/* Test-only exports called from Rust over FFI. */
size_t oracle_video_colorspace_size(void);
size_t oracle_video_colorspace_align(void);
size_t oracle_video_colorspace_count(void);
int oracle_video_colorspace_value(size_t i);
size_t oracle_video_range_type_size(void);
size_t oracle_video_range_type_align(void);
size_t oracle_video_range_type_count(void);
int oracle_video_range_type_value(size_t i);

/* Layout of enum video_colorspace. */
size_t oracle_video_colorspace_size(void)
{
	return sizeof(enum video_colorspace);
}
size_t oracle_video_colorspace_align(void)
{
	return _Alignof(enum video_colorspace);
}

/* Every enumerator, in header order. */
static const enum video_colorspace video_colorspaces[] = {
	VIDEO_CS_DEFAULT, VIDEO_CS_601, VIDEO_CS_709, VIDEO_CS_SRGB, VIDEO_CS_2100_PQ, VIDEO_CS_2100_HLG,
};

size_t oracle_video_colorspace_count(void)
{
	return sizeof(video_colorspaces) / sizeof(video_colorspaces[0]);
}
int oracle_video_colorspace_value(size_t i)
{
	return (int)video_colorspaces[i];
}

/* Layout of enum video_range_type. */
size_t oracle_video_range_type_size(void)
{
	return sizeof(enum video_range_type);
}
size_t oracle_video_range_type_align(void)
{
	return _Alignof(enum video_range_type);
}

/* Every enumerator, in header order. */
static const enum video_range_type video_range_types[] = {
	VIDEO_RANGE_DEFAULT,
	VIDEO_RANGE_PARTIAL,
	VIDEO_RANGE_FULL,
};

size_t oracle_video_range_type_count(void)
{
	return sizeof(video_range_types) / sizeof(video_range_types[0]);
}
int oracle_video_range_type_value(size_t i)
{
	return (int)video_range_types[i];
}
