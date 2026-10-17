/* Test-only oracle: the original libobs/media-io/video-frame.c, unmodified,
 * with its global symbols renamed to oracle_* so it can link next to the
 * Rust implementation, non-inline oracle_* wrappers for the static inline
 * helpers in video-frame.h, and the layout of struct video_frame as the C
 * compiler sees it. bmalloc, bfree and base_get_alignment are libobs's, so
 * under cargo test they resolve to the Rust bmem port. The enums are passed
 * as the ints they are. */
#include <stddef.h>

#define video_frame_get_linesizes oracle_video_frame_get_linesizes
#define video_frame_get_plane_heights oracle_video_frame_get_plane_heights
#define video_frame_init oracle_video_frame_init
#define video_frame_copy oracle_video_frame_copy

#include "media-io/video-frame.c.h"

/* Test-only exports called from Rust over FFI. */
void oracle_video_frame_free(struct video_frame *frame);
struct video_frame *oracle_video_frame_create(int format, uint32_t width, uint32_t height);
void oracle_video_frame_destroy(struct video_frame *frame);
size_t oracle_video_frame_size(void);
size_t oracle_video_frame_align(void);
size_t oracle_video_frame_planes(void);
size_t oracle_video_frame_linesize_planes(void);
size_t oracle_video_frame_offset_data(void);
size_t oracle_video_frame_offset_linesize(void);

/* The video-frame.h inlines; video_frame_create calls oracle_video_frame_init. */
void oracle_video_frame_free(struct video_frame *frame)
{
	video_frame_free(frame);
}
struct video_frame *oracle_video_frame_create(int format, uint32_t width, uint32_t height)
{
	return video_frame_create((enum video_format)format, width, height);
}
void oracle_video_frame_destroy(struct video_frame *frame)
{
	video_frame_destroy(frame);
}

/* Layout of struct video_frame. */
size_t oracle_video_frame_size(void)
{
	return sizeof(struct video_frame);
}
size_t oracle_video_frame_align(void)
{
	return _Alignof(struct video_frame);
}
size_t oracle_video_frame_planes(void)
{
	return sizeof(((struct video_frame *)0)->data) / sizeof(((struct video_frame *)0)->data[0]);
}
size_t oracle_video_frame_linesize_planes(void)
{
	return sizeof(((struct video_frame *)0)->linesize) / sizeof(((struct video_frame *)0)->linesize[0]);
}
size_t oracle_video_frame_offset_data(void)
{
	return offsetof(struct video_frame, data);
}
size_t oracle_video_frame_offset_linesize(void)
{
	return offsetof(struct video_frame, linesize);
}
