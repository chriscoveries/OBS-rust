/* Test-only oracle: the static inline helpers in libobs/media-io/video-io.h
 * are compiled into every caller, so this file exposes non-inline oracle_*
 * wrappers that call the real header functions, plus the layouts of
 * struct video_data, struct video_output_info and struct video_scale_info as
 * the C compiler sees them, and of enum video_trc and enum video_scale_type.
 * The enums are passed as the ints they are. */
#include <stddef.h>

#include "media-io/video-io.h"

bool oracle_format_is_yuv(int format)
{
	return format_is_yuv((enum video_format)format);
}
const char *oracle_get_video_format_name(int format)
{
	return get_video_format_name((enum video_format)format);
}
const char *oracle_get_video_colorspace_name(int cs)
{
	return get_video_colorspace_name((enum video_colorspace)cs);
}
int oracle_resolve_video_range(int format, int range)
{
	return (int)resolve_video_range((enum video_format)format, (enum video_range_type)range);
}
const char *oracle_get_video_range_name(int format, int range)
{
	return get_video_range_name((enum video_format)format, (enum video_range_type)range);
}

/* Layout of enum video_trc. */
size_t oracle_video_trc_size(void)
{
	return sizeof(enum video_trc);
}
size_t oracle_video_trc_align(void)
{
	return _Alignof(enum video_trc);
}

/* Every enumerator, in header order. */
static const enum video_trc video_trcs[] = {
	VIDEO_TRC_DEFAULT,
	VIDEO_TRC_SRGB,
	VIDEO_TRC_PQ,
	VIDEO_TRC_HLG,
};

size_t oracle_video_trc_count(void)
{
	return sizeof(video_trcs) / sizeof(video_trcs[0]);
}
int oracle_video_trc_value(size_t i)
{
	return (int)video_trcs[i];
}

/* Layout of enum video_scale_type. */
size_t oracle_video_scale_type_size(void)
{
	return sizeof(enum video_scale_type);
}
size_t oracle_video_scale_type_align(void)
{
	return _Alignof(enum video_scale_type);
}

/* Every enumerator, in header order. */
static const enum video_scale_type video_scale_types[] = {
	VIDEO_SCALE_DEFAULT, VIDEO_SCALE_POINT, VIDEO_SCALE_FAST_BILINEAR, VIDEO_SCALE_BILINEAR, VIDEO_SCALE_BICUBIC,
};

size_t oracle_video_scale_type_count(void)
{
	return sizeof(video_scale_types) / sizeof(video_scale_types[0]);
}
int oracle_video_scale_type_value(size_t i)
{
	return (int)video_scale_types[i];
}

/* Layout of struct video_data. */
size_t oracle_video_data_size(void)
{
	return sizeof(struct video_data);
}
size_t oracle_video_data_align(void)
{
	return _Alignof(struct video_data);
}
size_t oracle_video_data_planes(void)
{
	return sizeof(((struct video_data *)0)->data) / sizeof(((struct video_data *)0)->data[0]);
}
size_t oracle_video_data_offset_data(void)
{
	return offsetof(struct video_data, data);
}
size_t oracle_video_data_offset_linesize(void)
{
	return offsetof(struct video_data, linesize);
}
size_t oracle_video_data_offset_timestamp(void)
{
	return offsetof(struct video_data, timestamp);
}

/* Layout of struct video_output_info. */
size_t oracle_video_output_info_size(void)
{
	return sizeof(struct video_output_info);
}
size_t oracle_video_output_info_align(void)
{
	return _Alignof(struct video_output_info);
}
size_t oracle_video_output_info_offset_name(void)
{
	return offsetof(struct video_output_info, name);
}
size_t oracle_video_output_info_offset_format(void)
{
	return offsetof(struct video_output_info, format);
}
size_t oracle_video_output_info_offset_fps_num(void)
{
	return offsetof(struct video_output_info, fps_num);
}
size_t oracle_video_output_info_offset_fps_den(void)
{
	return offsetof(struct video_output_info, fps_den);
}
size_t oracle_video_output_info_offset_width(void)
{
	return offsetof(struct video_output_info, width);
}
size_t oracle_video_output_info_offset_height(void)
{
	return offsetof(struct video_output_info, height);
}
size_t oracle_video_output_info_offset_cache_size(void)
{
	return offsetof(struct video_output_info, cache_size);
}
size_t oracle_video_output_info_offset_colorspace(void)
{
	return offsetof(struct video_output_info, colorspace);
}
size_t oracle_video_output_info_offset_range(void)
{
	return offsetof(struct video_output_info, range);
}

/* Layout of struct video_scale_info. */
size_t oracle_video_scale_info_size(void)
{
	return sizeof(struct video_scale_info);
}
size_t oracle_video_scale_info_align(void)
{
	return _Alignof(struct video_scale_info);
}
size_t oracle_video_scale_info_offset_format(void)
{
	return offsetof(struct video_scale_info, format);
}
size_t oracle_video_scale_info_offset_width(void)
{
	return offsetof(struct video_scale_info, width);
}
size_t oracle_video_scale_info_offset_height(void)
{
	return offsetof(struct video_scale_info, height);
}
size_t oracle_video_scale_info_offset_range(void)
{
	return offsetof(struct video_scale_info, range);
}
size_t oracle_video_scale_info_offset_colorspace(void)
{
	return offsetof(struct video_scale_info, colorspace);
}
