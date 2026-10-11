#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <stdint.h>
#include <cmocka.h>

#include <util/c99defs.h>
#include <media-io/media-io-defs.h>

#include "avcapture-audio-planes.h"

/* OBSAVCaptureAudioFrame::data is a fixed table of MAX_AV_PLANES (8)
 * pointers; see MAX_AV_PLANES in libobs media-io-defs.h. The device
 * AudioBufferList may advertise more buffers than the table holds. */

static void test_under_limit_passes_through(void **state)
{
	UNUSED_PARAMETER(state);
	assert_int_equal(avcapture_audio_plane_count(0), 0);
	assert_int_equal(avcapture_audio_plane_count(1), 1);
	assert_int_equal(avcapture_audio_plane_count(7), MAX_AV_PLANES - 1);
}

static void test_at_limit_passes_through(void **state)
{
	UNUSED_PARAMETER(state);
	assert_int_equal(avcapture_audio_plane_count(MAX_AV_PLANES), MAX_AV_PLANES);
}

static void test_over_limit_is_clamped(void **state)
{
	UNUSED_PARAMETER(state);
	assert_int_equal(avcapture_audio_plane_count(MAX_AV_PLANES + 1), MAX_AV_PLANES);
	assert_int_equal(avcapture_audio_plane_count(64), MAX_AV_PLANES);
	assert_int_equal(avcapture_audio_plane_count(SIZE_MAX), MAX_AV_PLANES);
}

int main(void)
{
	const struct CMUnitTest tests[] = {
		cmocka_unit_test(test_under_limit_passes_through),
		cmocka_unit_test(test_at_limit_passes_through),
		cmocka_unit_test(test_over_limit_is_clamped),
	};
	return cmocka_run_group_tests(tests, NULL, NULL);
}
