/* Test-only oracle: the original libobs/media-io/video-frame.c, unmodified,
 * with its global symbols renamed to oracle_* so it can link next to the
 * Rust implementation, plus the struct layout as the C compiler sees it. */
#include <stddef.h>

#define video_frame_get_linesizes oracle_video_frame_get_linesizes
#define video_frame_get_plane_heights oracle_video_frame_get_plane_heights
#define video_frame_init oracle_video_frame_init
#define video_frame_copy oracle_video_frame_copy

#include "media-io/video-frame.c"

/* Layout of struct video_frame. */
size_t oracle_video_frame_size(void)
{
	return sizeof(struct video_frame);
}
size_t oracle_video_frame_align(void)
{
	return _Alignof(struct video_frame);
}
size_t oracle_video_frame_offset_data(void)
{
	return offsetof(struct video_frame, data);
}
size_t oracle_video_frame_offset_linesize(void)
{
	return offsetof(struct video_frame, linesize);
}
