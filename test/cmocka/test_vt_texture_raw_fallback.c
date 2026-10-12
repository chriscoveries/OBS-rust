#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <stdint.h>

#include <cmocka.h>

#define OBS_VT_ENCODER_TEST
#include "../../plugins/mac-videotoolbox/encoder.c"

static void init_test_encoder(struct vt_encoder *enc)
{
	memset(enc, 0, sizeof(*enc));
	assert_int_equal(pthread_mutex_init(&enc->texture_mutex, NULL), 0);
	assert_int_equal(pthread_cond_init(&enc->texture_cond, NULL), 0);
	enc->texture_sync_initialized = true;
}

static void destroy_test_encoder(struct vt_encoder *enc)
{
	vt_clear_texture_source_tombstones(enc);
	pthread_cond_destroy(&enc->texture_cond);
	pthread_mutex_destroy(&enc->texture_mutex);
}

static void test_texture_fallback_calls_raw_fallback_validation(void **state)
{
	UNUSED_PARAMETER(state);
	struct vt_encoder enc;
	init_test_encoder(&enc);
	enc.width = 1920;
	enc.height = 1080;
	enc.vt_pix_fmt = kCVPixelFormatType_420YpCbCr8BiPlanarVideoRange;
	enc.session = (VTCompressionSessionRef)(uintptr_t)1;

	CVPixelBufferRef wrong_size = NULL;
	assert_int_equal(CVPixelBufferCreate(kCFAllocatorDefault, 640, 480, enc.vt_pix_fmt, NULL, &wrong_size),
			 kCVReturnSuccess);
	assert_non_null(wrong_size);

	struct encoder_packet packet = {0};
	bool received_packet = false;
	assert_false(vt_encode_texture_raw_fallback(&enc, wrong_size, NULL, 0, &packet, &received_packet));
	assert_true(enc.texture_fallback_logged);
	assert_false(received_packet);

	CFRelease(wrong_size);
	destroy_test_encoder(&enc);
}

int main(void)
{
	const struct CMUnitTest tests[] = {
		cmocka_unit_test(test_texture_fallback_calls_raw_fallback_validation),
	};
	return cmocka_run_group_tests(tests, NULL, NULL);
}
