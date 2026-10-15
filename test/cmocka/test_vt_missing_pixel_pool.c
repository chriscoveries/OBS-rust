/* SPDX-License-Identifier: GPL-2.0-or-later */
/* The real encoder function with only its pool lookup replaced. */
#include <stdio.h>
#include <VideoToolbox/VideoToolbox.h>

static unsigned pool_lookups;

static CVPixelBufferPoolRef missing_pool(VTCompressionSessionRef session)
{
	(void)session;
	pool_lookups++;
	return NULL;
}

#define VTCompressionSessionGetPixelBufferPool missing_pool
#include "../../plugins/mac-videotoolbox/encoder.c"
#undef VTCompressionSessionGetPixelBufferPool

static int missing_pool_is_failure(void)
{
	struct vt_encoder encoder = {0};
	CVPixelBufferRef pixel_buffer = NULL;
	const bool result = get_cached_pixel_buffer(&encoder, &pixel_buffer);
	if (result) {
		fprintf(stderr, "missing_pool_is_failure: get_cached_pixel_buffer returned true for a NULL pool\n");
		return 1;
	}
	if (pixel_buffer != NULL || pool_lookups != 1) {
		fprintf(stderr, "missing_pool_is_failure: output pointer or pool lookup count changed\n");
		return 1;
	}
	return 0;
}

int main(void)
{
	const int failures = missing_pool_is_failure();
	printf("missing_pool_is_failure: failures=%d\n", failures);
	return failures;
}
