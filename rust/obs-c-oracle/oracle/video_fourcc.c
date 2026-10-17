/* Test-only oracle: the original libobs/media-io/video-fourcc.c, unmodified,
 * with its global symbols renamed to oracle_* so it can link next to the Rust
 * implementation, plus the enum video_format layout as the C compiler sees
 * it. */
#include <stddef.h>

#define video_format_from_fourcc oracle_video_format_from_fourcc

#include "media-io/video-fourcc.c.h"

/* Test-only exports called from Rust over FFI. */
size_t oracle_video_format_size(void);
size_t oracle_video_format_align(void);
size_t oracle_video_format_count(void);
int oracle_video_format_value(size_t i);

/* Layout of enum video_format. */
size_t oracle_video_format_size(void)
{
	return sizeof(enum video_format);
}
size_t oracle_video_format_align(void)
{
	return _Alignof(enum video_format);
}

/* Every enumerator, in header order, so the Rust discriminants can be
 * checked one by one. */
static const enum video_format video_formats[] = {
	VIDEO_FORMAT_NONE, VIDEO_FORMAT_I420, VIDEO_FORMAT_NV12, VIDEO_FORMAT_YVYU, VIDEO_FORMAT_YUY2,
	VIDEO_FORMAT_UYVY, VIDEO_FORMAT_RGBA, VIDEO_FORMAT_BGRA, VIDEO_FORMAT_BGRX, VIDEO_FORMAT_Y800,
	VIDEO_FORMAT_I444, VIDEO_FORMAT_BGR3, VIDEO_FORMAT_I422, VIDEO_FORMAT_I40A, VIDEO_FORMAT_I42A,
	VIDEO_FORMAT_YUVA, VIDEO_FORMAT_AYUV, VIDEO_FORMAT_I010, VIDEO_FORMAT_P010, VIDEO_FORMAT_I210,
	VIDEO_FORMAT_I412, VIDEO_FORMAT_YA2L, VIDEO_FORMAT_P216, VIDEO_FORMAT_P416, VIDEO_FORMAT_V210,
	VIDEO_FORMAT_R10L,
};

size_t oracle_video_format_count(void)
{
	return sizeof(video_formats) / sizeof(video_formats[0]);
}
int oracle_video_format_value(size_t i)
{
	return (int)video_formats[i];
}
