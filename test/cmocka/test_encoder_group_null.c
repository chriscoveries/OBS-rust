/* Regression test for obs-rust/obs-studio#483: obs_encoder_set_group(enc,
 * NULL) dropped the group's reference on the encoder but left
 * encoder->encoder_group pointing at the old group. A later set_group to
 * another group then released a reference the encoder no longer held, so the
 * encoder could be destroyed while the new group still listed it.
 *
 * The test holds two references and a weak reference, moves the encoder
 * first -> none -> second, drops its own references, and checks the second
 * group's reference keeps the encoder alive. Needs no video or display. */
#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <cmocka.h>

#include <stdio.h>

#include <obs.h>

#define VENC_ID "test_group_null_encoder"

static const char *enc_name(void *type_data)
{
	(void)type_data;
	return "group null encoder";
}

static void *enc_create(obs_data_t *settings, obs_encoder_t *encoder)
{
	(void)settings;
	(void)encoder;
	return bzalloc(1);
}

static void enc_destroy(void *data)
{
	bfree(data);
}

static bool enc_encode(void *data, struct encoder_frame *frame, struct encoder_packet *packet, bool *received)
{
	(void)data;
	(void)frame;
	(void)packet;
	*received = false;
	return true;
}

static bool obs_up;

static int group_setup(void **state)
{
	(void)state;
	obs_up = obs_startup("en-US", NULL, NULL);
	if (!obs_up) {
		fprintf(stderr, "skipped: obs_startup failed (no display?)\n");
		return 0;
	}

	struct obs_encoder_info info = {
		.id = VENC_ID,
		.type = OBS_ENCODER_VIDEO,
		.codec = "h264",
		.get_name = enc_name,
		.create = enc_create,
		.destroy = enc_destroy,
		.encode = enc_encode,
	};
	obs_register_encoder(&info);
	return 0;
}

static int group_teardown(void **state)
{
	(void)state;
	if (obs_up)
		obs_shutdown();
	return 0;
}

static void test_set_group_null_clears_group(void **state)
{
	(void)state;
	if (!obs_up)
		skip();
	obs_encoder_t *enc = obs_video_encoder_create(VENC_ID, "enc", NULL, NULL);
	assert_non_null(enc);
	obs_encoder_t *extra = obs_encoder_get_ref(enc);
	assert_ptr_equal(extra, enc);
	obs_weak_encoder_t *weak = obs_encoder_get_weak_encoder(enc);
	assert_non_null(weak);

	obs_encoder_group_t *first = obs_encoder_group_create();
	obs_encoder_group_t *second = obs_encoder_group_create();
	assert_non_null(first);
	assert_non_null(second);

	assert_true(obs_encoder_set_group(enc, first));
	assert_true(obs_encoder_set_group(enc, NULL));
	assert_true(obs_encoder_set_group(enc, second));

	/* Drop both of our references; only the second group's remains. */
	obs_encoder_release(extra);
	obs_encoder_release(enc);

	obs_encoder_t *alive = obs_weak_encoder_get_encoder(weak);
	assert_non_null(alive);
	obs_encoder_release(alive);

	/* Destroying the group drops the last reference. */
	obs_encoder_group_destroy(second);
	assert_null(obs_weak_encoder_get_encoder(weak));

	obs_weak_encoder_release(weak);
	obs_encoder_group_destroy(first);
}

int main(void)
{
	const struct CMUnitTest tests[] = {
		cmocka_unit_test(test_set_group_null_clears_group),
	};
	return cmocka_run_group_tests(tests, group_setup, group_teardown);
}
