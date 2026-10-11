/* Test-only oracle: libobs/media-io/frame-rate.h is header-only (static
 * inline), so this file exposes non-inline oracle_* wrappers that call the
 * real header functions, plus the layout of struct media_frames_per_second
 * as the C compiler sees it. */
#include <stddef.h>

/* frame-rate.h uses bool and uint32_t without including their headers. */
#include "util/c99defs.h"
#include "media-io/frame-rate.h"

double oracle_media_frames_per_second_to_frame_interval(struct media_frames_per_second fps)
{
	return media_frames_per_second_to_frame_interval(fps);
}
double oracle_media_frames_per_second_to_fps(struct media_frames_per_second fps)
{
	return media_frames_per_second_to_fps(fps);
}
bool oracle_media_frames_per_second_is_valid(struct media_frames_per_second fps)
{
	return media_frames_per_second_is_valid(fps);
}

/* Layout of struct media_frames_per_second. */
size_t oracle_media_frames_per_second_size(void)
{
	return sizeof(struct media_frames_per_second);
}
size_t oracle_media_frames_per_second_align(void)
{
	return _Alignof(struct media_frames_per_second);
}
size_t oracle_media_frames_per_second_offset_numerator(void)
{
	return offsetof(struct media_frames_per_second, numerator);
}
size_t oracle_media_frames_per_second_offset_denominator(void)
{
	return offsetof(struct media_frames_per_second, denominator);
}
