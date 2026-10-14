#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <setjmp.h>
#include <string.h>
#include <cmocka.h>

#include <pulse/pulseaudio.h>
#include <obs.h>
#include <util/bmem.h>

/* Exercise the production backend through the public monitoring APIs.
 * Only external libpulse calls are interposed: the real resampler, monitor
 * registry, capture callbacks and cleanup run in libobs in OFF and ON modes.
 * Synchronous replies make failed replacement initialization deterministic. */
struct test_context {
	pa_context_state_t state;
	unsigned disconnects;
	unsigned unrefs;
};

struct test_stream {
	struct test_context *context;
	unsigned disconnects;
	unsigned unrefs;
	unsigned playback_calls;
};

static struct test_context contexts[8];
static unsigned contexts_created;
static struct test_stream streams[8];
static unsigned streams_created;
static unsigned mainloops_created;
static unsigned mainloops_freed;
static bool fail_connect;
static bool fail_sink_query;
static int fake_object;
static uint64_t timestamp = 1000000000ULL;

pa_threaded_mainloop *pa_threaded_mainloop_new(void)
{
	mainloops_created++;
	return (pa_threaded_mainloop *)&fake_object;
}
int pa_threaded_mainloop_start(pa_threaded_mainloop *m)
{
	UNUSED_PARAMETER(m);
	return 0;
}
void pa_threaded_mainloop_stop(pa_threaded_mainloop *m)
{
	UNUSED_PARAMETER(m);
}
void pa_threaded_mainloop_free(pa_threaded_mainloop *m)
{
	UNUSED_PARAMETER(m);
	mainloops_freed++;
}
void pa_threaded_mainloop_lock(pa_threaded_mainloop *m)
{
	UNUSED_PARAMETER(m);
}
void pa_threaded_mainloop_unlock(pa_threaded_mainloop *m)
{
	UNUSED_PARAMETER(m);
}
void pa_threaded_mainloop_wait(pa_threaded_mainloop *m)
{
	UNUSED_PARAMETER(m);
}
void pa_threaded_mainloop_signal(pa_threaded_mainloop *m, int wait_for_accept)
{
	UNUSED_PARAMETER(m);
	UNUSED_PARAMETER(wait_for_accept);
}
pa_mainloop_api *pa_threaded_mainloop_get_api(pa_threaded_mainloop *m)
{
	UNUSED_PARAMETER(m);
	return NULL;
}

pa_context *pa_context_new_with_proplist(pa_mainloop_api *api, const char *name, const pa_proplist *p)
{
	UNUSED_PARAMETER(api);
	UNUSED_PARAMETER(name);
	UNUSED_PARAMETER(p);
	assert_true(contexts_created < sizeof(contexts) / sizeof(contexts[0]));
	struct test_context *context = &contexts[contexts_created++];
	context->state = PA_CONTEXT_READY;
	return (pa_context *)context;
}
void pa_context_set_state_callback(pa_context *c, pa_context_notify_cb_t cb, void *userdata)
{
	UNUSED_PARAMETER(c);
	UNUSED_PARAMETER(cb);
	UNUSED_PARAMETER(userdata);
}
int pa_context_connect(pa_context *c, const char *server, pa_context_flags_t flags, const pa_spawn_api *api)
{
	UNUSED_PARAMETER(c);
	UNUSED_PARAMETER(server);
	UNUSED_PARAMETER(flags);
	UNUSED_PARAMETER(api);
	return 0;
}
void pa_context_disconnect(pa_context *c)
{
	((struct test_context *)c)->disconnects++;
}
void pa_context_unref(pa_context *c)
{
	((struct test_context *)c)->unrefs++;
}
pa_context_state_t pa_context_get_state(const pa_context *c)
{
	const struct test_context *context = (const struct test_context *)c;
	assert_int_equal(context->unrefs, 0);
	return context->state;
}

pa_operation *pa_context_get_server_info(pa_context *c, pa_server_info_cb_t cb, void *userdata)
{
	pa_server_info info;
	memset(&info, 0, sizeof(info));
	info.default_sink_name = "test_sink";
	info.server_name = "test";
	info.server_version = "1";
	cb(c, &info, userdata);
	return (pa_operation *)&fake_object;
}
pa_operation_state_t pa_operation_get_state(const pa_operation *o)
{
	UNUSED_PARAMETER(o);
	return PA_OPERATION_DONE;
}
void pa_operation_unref(pa_operation *o)
{
	UNUSED_PARAMETER(o);
}

pa_operation *pa_context_get_sink_info_by_name(pa_context *c, const char *name, pa_sink_info_cb_t cb, void *userdata)
{
	UNUSED_PARAMETER(name);
	if (fail_sink_query)
		return NULL;
	pa_sink_info info = {0};
	info.index = 1;
	info.sample_spec = (pa_sample_spec){PA_SAMPLE_FLOAT32LE, 48000, 2};
	cb(c, &info, 0, userdata);
	cb(c, NULL, 1, userdata);
	return (pa_operation *)&fake_object;
}
void pa_context_set_subscribe_callback(pa_context *c, pa_context_subscribe_cb_t cb, void *userdata)
{
	UNUSED_PARAMETER(c);
	UNUSED_PARAMETER(cb);
	UNUSED_PARAMETER(userdata);
}
pa_operation *pa_context_subscribe(pa_context *c, pa_subscription_mask_t m, pa_context_success_cb_t cb, void *userdata)
{
	UNUSED_PARAMETER(c);
	UNUSED_PARAMETER(m);
	UNUSED_PARAMETER(cb);
	UNUSED_PARAMETER(userdata);
	return NULL;
}
pa_stream *pa_stream_new_with_proplist(pa_context *c, const char *name, const pa_sample_spec *ss,
				       const pa_channel_map *map, pa_proplist *p)
{
	UNUSED_PARAMETER(name);
	UNUSED_PARAMETER(ss);
	UNUSED_PARAMETER(map);
	UNUSED_PARAMETER(p);
	assert_true(streams_created < sizeof(streams) / sizeof(streams[0]));
	struct test_stream *stream = &streams[streams_created++];
	stream->context = (struct test_context *)c;
	return (pa_stream *)stream;
}
pa_context *pa_stream_get_context(const pa_stream *s)
{
	const struct test_stream *stream = (const struct test_stream *)s;
	assert_int_equal(stream->unrefs, 0);
	return (pa_context *)stream->context;
}
int pa_stream_connect_playback(pa_stream *s, const char *dev, const pa_buffer_attr *attr, pa_stream_flags_t flags,
			       const pa_cvolume *volume, pa_stream *sync_stream)
{
	UNUSED_PARAMETER(s);
	UNUSED_PARAMETER(dev);
	UNUSED_PARAMETER(attr);
	UNUSED_PARAMETER(flags);
	UNUSED_PARAMETER(volume);
	UNUSED_PARAMETER(sync_stream);
	return fail_connect ? -1 : 0;
}
void pa_stream_set_write_callback(pa_stream *s, pa_stream_request_cb_t cb, void *userdata)
{
	UNUSED_PARAMETER(s);
	UNUSED_PARAMETER(cb);
	UNUSED_PARAMETER(userdata);
}

int pa_stream_disconnect(pa_stream *s)
{
	((struct test_stream *)s)->disconnects++;
	return 0;
}

void pa_stream_unref(pa_stream *s)
{
	((struct test_stream *)s)->unrefs++;
}

int pa_stream_is_corked(const pa_stream *s)
{
	struct test_stream *stream = (struct test_stream *)s;
	assert_int_equal(stream->unrefs, 0);
	assert_non_null(stream->context);
	assert_int_equal(stream->context->state, PA_CONTEXT_READY);
	stream->playback_calls++;
	return 1;
}

static const char *source_name(void *unused)
{
	UNUSED_PARAMETER(unused);
	return "monitor reset source";
}

static void *source_create(obs_data_t *settings, obs_source_t *source)
{
	UNUSED_PARAMETER(settings);
	return source;
}

static void source_destroy(void *unused)
{
	UNUSED_PARAMETER(unused);
}

static void send_audio_with_calls(obs_source_t *source, struct test_stream *stream, unsigned calls)
{
	float samples[32] = {0};
	struct obs_source_audio audio = {
		.data = {(const uint8_t *)samples, (const uint8_t *)samples},
		.frames = 32,
		.speakers = SPEAKERS_STEREO,
		.format = AUDIO_FORMAT_FLOAT_PLANAR,
		.samples_per_sec = 48000,
		.timestamp = timestamp,
	};
	unsigned before = stream->playback_calls;
	timestamp += 1000000ULL;
	obs_source_output_audio(source, &audio);
	assert_int_equal(stream->playback_calls, before + calls);
}

static void send_audio(obs_source_t *source, struct test_stream *stream)
{
	send_audio_with_calls(source, stream, 1);
}

static void failed_reset_preserves_monitor_lifetime(void **state)
{
	UNUSED_PARAMETER(state);
	assert_true(obs_set_audio_monitoring_device("Test sink", "test_sink"));
	obs_source_t *source = obs_source_create_private("pulse_reset_source", "monitored", NULL);
	assert_non_null(source);
	obs_source_inc_active(source);
	obs_source_set_monitoring_enabled(source, true);
	assert_int_equal(streams_created, 1);
	send_audio(source, &streams[0]);

	/* Fail after the replacement owns a resampler and stream, twice. */
	fail_connect = true;
	for (unsigned i = 1; i <= 2; i++) {
		obs_reset_audio_monitoring();
		assert_int_equal(streams_created, i + 1);
		assert_int_equal(streams[i].disconnects, 1);
		assert_int_equal(streams[i].unrefs, 1);
		assert_int_equal(streams[0].disconnects, 0);
		assert_int_equal(streams[0].unrefs, 0);
		assert_int_equal(mainloops_created, 1);
		assert_int_equal(mainloops_freed, 0);
		send_audio(source, &streams[0]);
	}
	fail_connect = false;

	/* An earlier failure must also preserve the original live monitor. */
	fail_sink_query = true;
	obs_reset_audio_monitoring();
	fail_sink_query = false;
	assert_int_equal(streams_created, 3);
	assert_int_equal(streams[0].unrefs, 0);
	send_audio(source, &streams[0]);

	/* Destroy immediately after failed reset: every owned resource is
	 * released once, with no dangling resampler or device to release again. */
	obs_source_set_monitoring_enabled(source, false);
	assert_int_equal(streams[0].disconnects, 1);
	assert_int_equal(streams[0].unrefs, 1);
	assert_int_equal(mainloops_freed, 1);

	/* Recover from a failed replacement on the same healthy monitor, then
	 * repeat successful resets without disabling monitoring between them. */
	obs_source_set_monitoring_enabled(source, true);
	assert_int_equal(streams_created, 4);
	fail_connect = true;
	obs_reset_audio_monitoring();
	fail_connect = false;
	assert_int_equal(streams_created, 5);
	assert_int_equal(streams[3].unrefs, 0);
	assert_int_equal(streams[4].unrefs, 1);
	send_audio(source, &streams[3]);
	for (unsigned i = 5; i <= 6; i++) {
		unsigned previous = streams_created - 1;
		if (i == 5)
			previous = 3;
		obs_reset_audio_monitoring();
		assert_int_equal(streams_created, i + 1);
		assert_int_equal(streams[previous].disconnects, 1);
		assert_int_equal(streams[previous].unrefs, 1);
		assert_int_equal(streams[i].unrefs, 0);
		send_audio(source, &streams[i]);
	}
	obs_source_set_monitoring_enabled(source, false);
	assert_int_equal(streams[6].disconnects, 1);
	assert_int_equal(streams[6].unrefs, 1);
	assert_int_equal(mainloops_created, 2);
	assert_int_equal(mainloops_created, mainloops_freed);
	obs_source_dec_active(source);
	obs_source_release(source);
}

static void terminate_context(struct test_context *context, pa_context_state_t terminal_state)
{
	context->state = terminal_state;
	/* PulseAudio detaches streams on a terminal context transition. The
	 * monitor's own stream reference remains valid until it is unrefed. */
	for (unsigned i = 0; i < streams_created; i++) {
		if (streams[i].context == context)
			streams[i].context = NULL;
	}
}

static void terminal_context_reset_recovers(pa_context_state_t terminal_state, unsigned monitor_count)
{
	assert_true(obs_set_audio_monitoring_device("Test sink", "test_sink"));
	obs_source_t *sources[2];
	assert_true(monitor_count <= sizeof(sources) / sizeof(sources[0]));
	for (unsigned i = 0; i < monitor_count; i++) {
		sources[i] = obs_source_create_private("pulse_reset_source", "monitored", NULL);
		assert_non_null(sources[i]);
		obs_source_inc_active(sources[i]);
		obs_source_set_monitoring_enabled(sources[i], true);
		send_audio(sources[i], &streams[i]);
	}
	assert_int_equal(streams_created, monitor_count);
	assert_int_equal(contexts_created, 1);

	/* A disconnected context stays terminal; only a newly created context
	 * becomes READY, modeling an available daemon after each restart. */
	for (unsigned restart = 1; restart <= 3; restart++) {
		terminate_context(&contexts[restart - 1], terminal_state);
		obs_reset_audio_monitoring();
		assert_int_equal(contexts_created, restart + 1);
		assert_int_equal(streams_created, monitor_count * (restart + 1));
		assert_int_equal(contexts[restart - 1].disconnects, 1);
		assert_int_equal(contexts[restart - 1].unrefs, 1);
		assert_int_equal(contexts[restart].state, PA_CONTEXT_READY);
		assert_int_equal(mainloops_created - mainloops_freed, 1);
		for (unsigned i = 0; i < monitor_count; i++) {
			unsigned previous = monitor_count * (restart - 1) + i;
			assert_int_equal(streams[previous].disconnects, 1);
			assert_int_equal(streams[previous].unrefs, 1);
			send_audio(sources[i], &streams[monitor_count * restart + i]);
		}
		print_message("restart/reset %u: contexts=%u streams=%u mainloops=%u freed=%u\n", restart,
			      contexts_created, streams_created, mainloops_created, mainloops_freed);
	}
	for (unsigned i = 0; i < monitor_count; i++) {
		obs_source_set_monitoring_enabled(sources[i], false);
		assert_int_equal(streams[monitor_count * 3 + i].disconnects, 1);
		assert_int_equal(streams[monitor_count * 3 + i].unrefs, 1);
		obs_source_dec_active(sources[i]);
		obs_source_release(sources[i]);
	}
	assert_int_equal(contexts[3].disconnects, 1);
	assert_int_equal(contexts[3].unrefs, 1);
	assert_int_equal(mainloops_created, mainloops_freed);
}

static void failed_context_reset_recovers(void **state)
{
	UNUSED_PARAMETER(state);
	terminal_context_reset_recovers(PA_CONTEXT_FAILED, 1);
}

static void terminated_context_reset_recovers(void **state)
{
	UNUSED_PARAMETER(state);
	terminal_context_reset_recovers(PA_CONTEXT_TERMINATED, 1);
}

static void shared_failed_context_reset_recovers(void **state)
{
	UNUSED_PARAMETER(state);
	terminal_context_reset_recovers(PA_CONTEXT_FAILED, 2);
}

static void failed_context_replacement_failure_is_safe(void **state)
{
	UNUSED_PARAMETER(state);
	assert_true(obs_set_audio_monitoring_device("Test sink", "test_sink"));
	obs_source_t *source = obs_source_create_private("pulse_reset_source", "monitored", NULL);
	assert_non_null(source);
	obs_source_inc_active(source);
	obs_source_set_monitoring_enabled(source, true);
	assert_int_equal(streams_created, 1);
	send_audio(source, &streams[0]);

	terminate_context(&contexts[0], PA_CONTEXT_FAILED);
	fail_sink_query = true;
	obs_reset_audio_monitoring();
	fail_sink_query = false;
	assert_int_equal(contexts_created, 2);
	assert_int_equal(contexts[0].disconnects, 1);
	assert_int_equal(contexts[0].unrefs, 1);
	assert_int_equal(contexts[1].unrefs, 1);
	assert_int_equal(mainloops_created, mainloops_freed);
	assert_int_equal(streams[0].disconnects, 1);
	assert_int_equal(streams[0].unrefs, 1);
	send_audio_with_calls(source, &streams[0], 0);

	/* Retiring the failed context must leave owned resources safe to retry,
	 * even when the first replacement also failed. */
	obs_reset_audio_monitoring();
	assert_int_equal(contexts_created, 3);
	assert_int_equal(streams_created, 2);
	assert_int_equal(streams[0].disconnects, 1);
	assert_int_equal(streams[0].unrefs, 1);
	send_audio(source, &streams[1]);

	/* Immediate destruction after another failed replacement must be safe. */
	terminate_context(&contexts[2], PA_CONTEXT_FAILED);
	fail_sink_query = true;
	obs_reset_audio_monitoring();
	fail_sink_query = false;
	assert_int_equal(contexts_created, 4);
	assert_int_equal(mainloops_created, mainloops_freed);
	send_audio_with_calls(source, &streams[1], 0);
	obs_source_set_monitoring_enabled(source, false);
	assert_int_equal(streams[1].disconnects, 1);
	assert_int_equal(streams[1].unrefs, 1);
	assert_int_equal(mainloops_created, mainloops_freed);
	for (unsigned i = 0; i < contexts_created; i++) {
		assert_int_equal(contexts[i].disconnects, 1);
		assert_int_equal(contexts[i].unrefs, 1);
	}
	obs_source_dec_active(source);
	obs_source_release(source);
}

int main(int argc, char **argv)
{
	long before = bnum_allocs();
	if (!obs_startup("en-US", NULL, NULL))
		return 77;
	struct obs_audio_info audio = {.samples_per_sec = 48000, .speakers = SPEAKERS_STEREO};
	if (!obs_reset_audio(&audio))
		return 1;
	struct obs_source_info info = {
		.id = "pulse_reset_source",
		.type = OBS_SOURCE_TYPE_INPUT,
		.output_flags = OBS_SOURCE_AUDIO,
		.get_name = source_name,
		.create = source_create,
		.destroy = source_destroy,
	};
	obs_register_source(&info);

	cmocka_set_test_filter(argc > 1 ? argv[1] : "failed_reset_preserves_monitor_lifetime");
	const struct CMUnitTest tests[] = {
		cmocka_unit_test(failed_reset_preserves_monitor_lifetime),
		cmocka_unit_test(failed_context_reset_recovers),
		cmocka_unit_test(terminated_context_reset_recovers),
		cmocka_unit_test(shared_failed_context_reset_recovers),
		cmocka_unit_test(failed_context_replacement_failure_is_safe),
	};
	int result = cmocka_run_group_tests(tests, NULL, NULL);
	/* On a regression, the assertion has already identified a released live
	 * monitor. Exit without asking shutdown to release its dangling state. */
	if (result)
		return result;
	obs_shutdown();
	if (bnum_allocs() != before) {
		fprintf(stderr, "Unbalanced libobs allocations: %ld (expected %ld)\n", bnum_allocs(), before);
		return 1;
	}
	return 0;
}
