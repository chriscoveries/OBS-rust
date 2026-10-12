/* Two pre-existing caption-queue lifecycle bugs in libobs/obs-output.c:
 *
 * 1. obs_output_actual_stop() drains ctrack->caption_head/caption_tail
 *    without holding ctrack->caption_mutex. Every writer
 *    (obs_output_output_caption_text2) and reader (the encoder caption
 *    interleave path) takes that mutex; the drain can free nodes while
 *    another thread walks or appends the same list.
 *
 * 2. destroy_caption_track() frees the track without draining pending
 *    caption_text nodes. An output destroyed with queued captions leaks
 *    every queued node.
 *
 * Detection without sanitizers: the stop drain is observed by holding the
 * mutex during obs_output_stop() on another thread — a correct drain
 * blocks until the mutex is released, so the queue is still non-empty
 * while the lock is held. The leak is observed through bnum_allocs().
 */
#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <cmocka.h>

#include <stdio.h>
#include <string.h>

#include <obs.h>
#include <util/bmem.h>
#include <util/threading.h>

#include "obs-internal.h"

static int obs_up;

/* ------------------------------------------------------------------------- */

static const char *enc_name(void *type_data)
{
	(void)type_data;
	return "test encoder";
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

static const char *out_name(void *type_data)
{
	(void)type_data;
	return "test output";
}

static void *out_create(obs_data_t *settings, obs_output_t *output)
{
	(void)settings;
	return output;
}

static void out_destroy(void *data)
{
	(void)data;
}

static bool out_start(void *data)
{
	(void)data;
	return false;
}

static void out_stop(void *data, uint64_t ts)
{
	(void)ts;
	/* what a real encoded output does from .stop: ends data capture and
	 * re-signals stopping_event */
	obs_output_end_data_capture(data);
}

static void out_packet(void *data, struct encoder_packet *packet)
{
	(void)data;
	(void)packet;
}

static void register_types(void)
{
	struct obs_encoder_info enc_info = {
		.id = "test_caption_queue_encoder",
		.type = OBS_ENCODER_VIDEO,
		.codec = "h264",
		.get_name = enc_name,
		.create = enc_create,
		.destroy = enc_destroy,
		.encode = enc_encode,
	};
	obs_register_encoder(&enc_info);

	struct obs_output_info out_info = {
		.id = "test_caption_queue_output",
		.flags = OBS_OUTPUT_ENCODED | OBS_OUTPUT_VIDEO,
		.get_name = out_name,
		.create = out_create,
		.destroy = out_destroy,
		.start = out_start,
		.stop = out_stop,
		.encoded_packet = out_packet,
	};
	obs_register_output(&out_info);
}

/* ------------------------------------------------------------------------- */

struct caption_harness {
	obs_output_t *output;
	obs_encoder_t *encoder;
};

static struct caption_harness *harness_create(void)
{
	struct caption_harness *h = bzalloc(sizeof(*h));
	h->output = obs_output_create("test_caption_queue_output", "test", NULL, NULL);
	h->encoder = obs_video_encoder_create("test_caption_queue_encoder", "test enc", NULL, NULL);
	assert_non_null(h->output);
	assert_non_null(h->encoder);

	obs_output_set_video_encoder(h->output, h->encoder);
	assert_non_null(h->output->caption_tracks[0]);

	/* begin_data_capture would need a running core video; setting active
	 * is enough for obs_output_output_caption_text2() */
	os_atomic_set_bool(&h->output->active, true);
	return h;
}

static void harness_destroy(struct caption_harness *h)
{
	struct caption_track_data *ctrack = h->output->caption_tracks[0];

	/* drain whatever the code under test left queued, so teardown is
	 * clean whether or not the fix is in place */
	pthread_mutex_lock(&ctrack->caption_mutex);
	struct caption_text *node = ctrack->caption_head;
	while (node) {
		struct caption_text *next = node->next;
		bfree(node);
		node = next;
	}
	ctrack->caption_head = NULL;
	ctrack->caption_tail = NULL;
	pthread_mutex_unlock(&ctrack->caption_mutex);

	os_atomic_set_bool(&h->output->active, false);
	obs_encoder_release(h->encoder);
	obs_output_release(h->output);
	bfree(h);
}

/* ------------------------------------------------------------------------- */

struct stop_args {
	obs_output_t *output;
	pthread_t thread;
};

static void *stop_thread(void *arg)
{
	obs_output_t *output = arg;
	obs_output_stop(output);
	return NULL;
}

/* The stop drain must hold caption_mutex: while this test holds it, the
 * queue must not be emptied by a concurrent obs_output_stop(). */
static void caption_stop_drain_holds_mutex(void **state)
{
	(void)state;
	struct caption_harness *h = harness_create();
	struct caption_track_data *ctrack = h->output->caption_tracks[0];

	obs_output_output_caption_text2(h->output, "first", 2.0);
	obs_output_output_caption_text2(h->output, "second", 2.0);
	assert_non_null(ctrack->caption_head);

	pthread_t thread;
	pthread_mutex_lock(&ctrack->caption_mutex);
	assert_int_equal(0, pthread_create(&thread, NULL, stop_thread, h->output));

	/* give the stop thread a moment to reach the drain */
	os_sleep_ms(200);

	/* while we hold the mutex the queue must still be intact: a correct
	 * drain is blocked on it; the buggy one already freed the nodes */
	bool still_queued = ctrack->caption_head != NULL;

	pthread_mutex_unlock(&ctrack->caption_mutex);
	pthread_join(thread, NULL);

	assert_true(still_queued);
	assert_null(ctrack->caption_head);

	harness_destroy(h);
}

/* Destroying an output with queued captions must not leak the nodes. */
static void caption_track_destroy_drains(void **state)
{
	(void)state;

	/* warm-up pass so one-time allocations do not pollute the delta */
	{
		struct caption_harness *h = harness_create();
		obs_output_output_caption_text2(h->output, "warmup", 2.0);
		harness_destroy(h);
	}

	long before = bnum_allocs();
	{
		struct caption_harness *h = harness_create();
		obs_output_output_caption_text2(h->output, "leaked", 2.0);
		obs_output_output_caption_text2(h->output, "also leaked", 2.0);
		/* release with the queue still full: destroy_caption_track must
		 * free these nodes */
		os_atomic_set_bool(&h->output->active, false);
		obs_encoder_release(h->encoder);
		obs_output_release(h->output);
		bfree(h);
	}
	assert_int_equal(before, bnum_allocs());
}

int main(void)
{
	obs_up = obs_startup("en-US", NULL, NULL);
	if (!obs_up) {
		puts("obs_startup failed, skipping");
		return 0;
	}
	register_types();

	const struct CMUnitTest tests[] = {
		cmocka_unit_test(caption_stop_drain_holds_mutex),
		cmocka_unit_test(caption_track_destroy_drains),
	};
	int rc = cmocka_run_group_tests(tests, NULL, NULL);

	obs_shutdown();
	return rc;
}
