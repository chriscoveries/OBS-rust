/* Regression test for encoder group lifetime conformance.
 *
 * obs_encoder_set_group(encoder, NULL) removes the encoder from its group and
 * drops the reference the group held, but it left encoder->encoder_group
 * pointing at the group it just left. The next obs_encoder_set_group() call
 * (or the one obs_encoder_destroy() makes) then believed the encoder was still
 * a member of that group and dropped a reference the encoder no longer owned,
 * so the encoder was destroyed while a group and its callers still held
 * references to it. The stale pointer also dangled once the old group was
 * destroyed.
 *
 * The test only uses the public API. It holds two references of its own, moves
 * the encoder out of a group and into another one, and then releases both of
 * its references. The group's reference must keep the encoder alive. The
 * weak reference taken up front tells whether the encoder is still alive.
 */
#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <cmocka.h>

#include <stdio.h>

#include <obs.h>

#define ENCODER_ID "test_group_clear_encoder"

static int obs_up;

static const char *enc_name(void *type_data)
{
	(void)type_data;
	return "group clear encoder";
}

static void *enc_create(obs_data_t *settings, obs_encoder_t *encoder)
{
	(void)settings;
	return encoder;
}

static void enc_destroy(void *data)
{
	(void)data;
}

static bool enc_encode(void *data, struct encoder_frame *frame, struct encoder_packet *packet, bool *received_packet)
{
	(void)data;
	(void)frame;
	(void)packet;
	*received_packet = false;
	return true;
}

static void register_encoder(void)
{
	struct obs_encoder_info info = {
		.id = ENCODER_ID,
		.type = OBS_ENCODER_VIDEO,
		.codec = "h264",
		.get_name = enc_name,
		.create = enc_create,
		.destroy = enc_destroy,
		.encode = enc_encode,
	};
	obs_register_encoder(&info);
}

static void leaving_group_does_not_drop_a_later_reference(void **state)
{
	(void)state;
	if (!obs_up) {
		skip();
		return;
	}

	obs_encoder_t *encoder = obs_video_encoder_create(ENCODER_ID, "group clear", NULL, NULL);
	assert_non_null(encoder);
	obs_encoder_t *extra = obs_encoder_get_ref(encoder);
	assert_non_null(extra);
	obs_weak_encoder_t *weak = obs_encoder_get_weak_encoder(encoder);
	assert_non_null(weak);

	obs_encoder_group_t *first = obs_encoder_group_create();
	obs_encoder_group_t *second = obs_encoder_group_create();
	assert_non_null(first);
	assert_non_null(second);

	/* in the first group, out of it, then in the second group */
	assert_true(obs_encoder_set_group(encoder, first));
	assert_true(obs_encoder_set_group(encoder, NULL));
	assert_true(obs_encoder_set_group(encoder, second));

	/* the second group now holds the only reference besides ours */
	obs_encoder_release(encoder);
	obs_encoder_release(extra);
	obs_encoder_t *alive_while_grouped = obs_weak_encoder_get_encoder(weak);
	obs_encoder_release(alive_while_grouped);

	/* releasing the group releases the encoder, if it is still alive */
	obs_encoder_group_destroy(second);
	obs_encoder_group_destroy(first);
	obs_encoder_t *alive_after_groups = obs_weak_encoder_get_encoder(weak);
	obs_encoder_release(alive_after_groups);
	obs_weak_encoder_release(weak);

	assert_non_null(alive_while_grouped);
	assert_null(alive_after_groups);
}

static int group_setup(void **state)
{
	(void)state;
	obs_up = obs_startup("en-US", NULL, NULL);
	if (obs_up)
		register_encoder();
	else
		fprintf(stderr, "cases skipped: obs_startup failed (no display?)\n");
	return 0;
}

static int group_teardown(void **state)
{
	(void)state;
	if (obs_up)
		obs_shutdown();
	return 0;
}

int main(void)
{
	const struct CMUnitTest tests[] = {
		cmocka_unit_test(leaving_group_does_not_drop_a_later_reference),
	};
	return cmocka_run_group_tests(tests, group_setup, group_teardown);
}
