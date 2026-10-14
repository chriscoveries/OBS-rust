#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>

#include <cmocka.h>
#include <media-playback/media-playback.h>

#include "obs-ffmpeg-source.c"

/* Long enough that a scheduled reconnect thread only finishes when the test
 * stops it, so the test never depends on the reconnect timer. */
#define TEST_RECONNECT_DELAY_SEC 3600
/* One second of frames at 60 fps for the restart-storm case. */
#define TEST_STORM_FRAMES 60
/* Room for one open and one close per storm frame, plus the initial media. */
#define TEST_MAX_MEDIA (TEST_STORM_FRAMES + 4)

static bool source_showing;
static bool restart_queued;

static max_align_t created_media[TEST_MAX_MEDIA];
static unsigned media_create_count;
static media_playback_t *destroyed_media[TEST_MAX_MEDIA];
static unsigned media_destroy_count;
static media_playback_t *last_played_media;
static unsigned media_play_count;

const char *__wrap_obs_module_text(const char *text)
{
	return text;
}

const char *__wrap_obs_source_get_name(const obs_source_t *source)
{
	UNUSED_PARAMETER(source);
	return "test_ffmpeg_restart_close";
}

bool __wrap_obs_source_showing(const obs_source_t *source)
{
	UNUSED_PARAMETER(source);
	return source_showing;
}

bool __wrap_obs_source_active(const obs_source_t *source)
{
	UNUSED_PARAMETER(source);
	return true;
}

void __wrap_obs_source_media_ended(obs_source_t *source)
{
	UNUSED_PARAMETER(source);
	/* Models a media_ended signal subscriber queuing a restart. */
	restart_queued = true;
}

void __wrap_obs_source_media_started(obs_source_t *source)
{
	UNUSED_PARAMETER(source);
}

void __wrap_obs_source_output_video(obs_source_t *source, const struct obs_source_frame *frame)
{
	UNUSED_PARAMETER(source);
	UNUSED_PARAMETER(frame);
}

media_playback_t *__wrap_media_playback_create(const struct mp_media_info *info)
{
	UNUSED_PARAMETER(info);
	assert_true(media_create_count < TEST_MAX_MEDIA);
	return (media_playback_t *)&created_media[media_create_count++];
}

void __wrap_media_playback_play(media_playback_t *media, bool looping, bool reconnecting)
{
	UNUSED_PARAMETER(looping);
	UNUSED_PARAMETER(reconnecting);
	last_played_media = media;
	media_play_count++;
}

bool __wrap_media_playback_has_video(media_playback_t *media)
{
	UNUSED_PARAMETER(media);
	return true;
}

void __wrap_media_playback_destroy(media_playback_t *media)
{
	assert_true(media_destroy_count < TEST_MAX_MEDIA);
	destroyed_media[media_destroy_count++] = media;
}

static unsigned times_destroyed(const media_playback_t *media)
{
	unsigned count = 0;
	for (unsigned i = 0; i < media_destroy_count; i++) {
		if (destroyed_media[i] == media)
			count++;
	}
	return count;
}

static int reset_wrappers(void **state)
{
	UNUSED_PARAMETER(state);
	source_showing = true;
	restart_queued = false;
	media_create_count = 0;
	media_destroy_count = 0;
	last_played_media = NULL;
	media_play_count = 0;
	return 0;
}

static void init_network_source(struct ffmpeg_source *s)
{
	s->is_local_file = false;
	s->input = (char *)"rtmp://example.invalid/live";
	s->reconnect_delay_sec = TEST_RECONNECT_DELAY_SEC;
	assert_int_equal(os_event_init(&s->reconnect_stop_event, OS_EVENT_TYPE_MANUAL), 0);
	assert_int_equal(pthread_mutex_init(&s->reconnect_mutex, NULL), 0);
}

static void free_network_source(struct ffmpeg_source *s)
{
	stop_reconnect_thread(s);
	pthread_mutex_destroy(&s->reconnect_mutex);
	os_event_destroy(s->reconnect_stop_event);
}

/* A local close-when-inactive file reaches EOF and a media_ended subscriber
 * queues a restart. obs_source_video_tick processes the queued restart before
 * the source's video_tick, so drive the callbacks in that order directly; the
 * test needs no sleeps or GUI. The restarted media must survive the tick. */
static void restart_after_eof_keeps_media_playing(void **state)
{
	UNUSED_PARAMETER(state);

	max_align_t ended_handle;
	max_align_t source_handle;
	media_playback_t *ended = (media_playback_t *)&ended_handle;
	struct ffmpeg_source s = {
		.media = ended,
		.source = (obs_source_t *)&source_handle,
		.input = (char *)"clip.mkv",
		.is_local_file = true,
		.close_when_inactive = true,
		.state = OBS_MEDIA_STATE_PLAYING,
	};

	/* The EOF callback marks the media for closing, then notifies listeners. */
	media_stopped(&s);
	assert_true(s.destroy_media);
	assert_true(restart_queued);

	ffmpeg_source_restart(&s);
	ffmpeg_source_tick(&s, 0.0f);

	assert_non_null(s.media);
	assert_ptr_equal(s.media, last_played_media);
	assert_int_equal(times_destroyed(s.media), 0);
	assert_int_equal(s.state, OBS_MEDIA_STATE_PLAYING);
	assert_false(s.destroy_media);

	/* The ended media may belong to a worker that has already exited, so the
	 * restart must not reuse it: it is closed once and new media is opened. */
	assert_ptr_not_equal(s.media, ended);
	assert_int_equal(times_destroyed(ended), 1);
	assert_int_equal(media_create_count, 1);
	assert_int_equal(media_play_count, 1);
}

/* The worker of a network source exits when the stream drops or an open or
 * read fails, and media_stopped requests a close and reconnect. A restart
 * queued in that window must not keep the dead media, and must not reopen the
 * stream at once either: the tick closes the media and the reconnect waits
 * for reconnect_delay_sec, as it does without the restart. */
static void restart_after_network_failure_waits_for_reconnect(void **state)
{
	UNUSED_PARAMETER(state);

	max_align_t dead_handle;
	max_align_t source_handle;
	media_playback_t *dead = (media_playback_t *)&dead_handle;
	struct ffmpeg_source s = {
		.media = dead,
		.source = (obs_source_t *)&source_handle,
		.state = OBS_MEDIA_STATE_PLAYING,
	};
	init_network_source(&s);

	media_stopped(&s);
	assert_true(s.destroy_media);

	ffmpeg_source_restart(&s);
	ffmpeg_source_tick(&s, 0.0f);

	media_playback_t *media = s.media;
	bool reconnect_scheduled = s.reconnect_thread_valid;
	free_network_source(&s);

	assert_int_equal(times_destroyed(dead), 1);
	assert_null(media);
	assert_int_equal(media_create_count, 0);
	assert_true(reconnect_scheduled);
}

/* A media_ended subscriber that always restarts, against a server that is
 * down: every frame the current media's worker fails, the queued restart
 * runs, then the source ticks. Over a second of frames the source must not
 * open the stream once per frame; it waits for the single delayed reconnect. */
static void restart_storm_against_down_server_waits_for_reconnect(void **state)
{
	UNUSED_PARAMETER(state);

	max_align_t dead_handle;
	max_align_t source_handle;
	struct ffmpeg_source s = {
		.media = (media_playback_t *)&dead_handle,
		.source = (obs_source_t *)&source_handle,
		.state = OBS_MEDIA_STATE_PLAYING,
	};
	init_network_source(&s);
	unsigned reconnects_started = 0;

	for (int frame = 0; frame < TEST_STORM_FRAMES; frame++) {
		/* The current media's worker fails to open and exits. */
		if (s.media)
			media_stopped(&s);
		if (restart_queued) {
			restart_queued = false;
			ffmpeg_source_restart(&s);
		}
		bool reconnect_was_scheduled = s.reconnect_thread_valid;
		ffmpeg_source_tick(&s, 1.0f / 60.0f);
		if (s.reconnect_thread_valid && !reconnect_was_scheduled)
			reconnects_started++;
	}

	free_network_source(&s);

	assert_int_equal(media_create_count, 0);
	assert_int_equal(reconnects_started, 1);
}

/* A restart of a hidden source does not start playback, so it leaves the
 * pending close from EOF to the next tick, which closes the media. */
static void hidden_restart_still_closes_local_media(void **state)
{
	UNUSED_PARAMETER(state);

	max_align_t ended_handle;
	max_align_t source_handle;
	media_playback_t *ended = (media_playback_t *)&ended_handle;
	struct ffmpeg_source s = {
		.media = ended,
		.source = (obs_source_t *)&source_handle,
		.input = (char *)"clip.mkv",
		.is_local_file = true,
		.close_when_inactive = true,
		.state = OBS_MEDIA_STATE_PLAYING,
	};
	source_showing = false;

	media_stopped(&s);
	ffmpeg_source_restart(&s);
	assert_ptr_equal(s.media, ended);
	assert_int_equal(media_destroy_count, 0);

	ffmpeg_source_tick(&s, 0.0f);

	assert_null(s.media);
	assert_int_equal(times_destroyed(ended), 1);
	assert_int_equal(media_create_count, 0);
	assert_int_equal(media_play_count, 0);
	assert_false(s.destroy_media);
}

/* Likewise, a hidden network source whose worker died still reconnects. */
static void hidden_restart_still_reconnects_network_media(void **state)
{
	UNUSED_PARAMETER(state);

	max_align_t dead_handle;
	max_align_t source_handle;
	media_playback_t *dead = (media_playback_t *)&dead_handle;
	struct ffmpeg_source s = {
		.media = dead,
		.source = (obs_source_t *)&source_handle,
		.state = OBS_MEDIA_STATE_PLAYING,
	};
	init_network_source(&s);
	source_showing = false;

	media_stopped(&s);
	ffmpeg_source_restart(&s);
	ffmpeg_source_tick(&s, 0.0f);

	media_playback_t *media = s.media;
	bool reconnect_scheduled = s.reconnect_thread_valid;
	free_network_source(&s);

	assert_null(media);
	assert_int_equal(times_destroyed(dead), 1);
	assert_int_equal(media_play_count, 0);
	assert_true(reconnect_scheduled);
}

/* A media_ended subscriber that switches files (a playlist script) updates the
 * settings. libobs defers that update to obs_source_video_tick, after queued
 * media actions and before the source's video_tick, so the update replaces the
 * media while the close from EOF is still pending. The update opens the new
 * media once, and the tick that follows must not close it. */
static void update_with_pending_close_keeps_new_media(void **state)
{
	UNUSED_PARAMETER(state);

	max_align_t ended_handle;
	max_align_t source_handle;
	media_playback_t *ended = (media_playback_t *)&ended_handle;
	struct ffmpeg_source s = {
		.media = ended,
		.source = (obs_source_t *)&source_handle,
		.input = bstrdup("clip.mkv"),
		.speed_percent = 100,
		.is_local_file = true,
		.close_when_inactive = true,
		.state = OBS_MEDIA_STATE_PLAYING,
	};

	media_stopped(&s);
	assert_true(s.destroy_media);

	obs_data_t *settings = obs_data_create();
	obs_data_set_bool(settings, "is_local_file", true);
	obs_data_set_string(settings, "local_file", "other.mkv");
	obs_data_set_bool(settings, "close_when_inactive", true);
	obs_data_set_int(settings, "speed_percent", 100);
	ffmpeg_source_update(&s, settings);
	obs_data_release(settings);

	media_playback_t *opened = s.media;
	unsigned creates_after_update = media_create_count;
	unsigned destroys_after_update = media_destroy_count;

	ffmpeg_source_tick(&s, 0.0f);

	media_playback_t *media = s.media;
	bool close_pending = s.destroy_media;
	bfree(s.input);
	bfree(s.input_format);
	bfree(s.ffmpeg_options);

	assert_int_equal(times_destroyed(ended), 1);
	assert_int_equal(creates_after_update, 1);
	assert_int_equal(destroys_after_update, 1);
	assert_non_null(opened);
	assert_ptr_equal(opened, last_played_media);

	assert_ptr_equal(media, opened);
	assert_int_equal(times_destroyed(opened), 0);
	assert_false(close_pending);
}

int main(void)
{
	const struct CMUnitTest tests[] = {
		cmocka_unit_test_setup(restart_after_eof_keeps_media_playing, reset_wrappers),
		cmocka_unit_test_setup(restart_after_network_failure_waits_for_reconnect, reset_wrappers),
		cmocka_unit_test_setup(restart_storm_against_down_server_waits_for_reconnect, reset_wrappers),
		cmocka_unit_test_setup(hidden_restart_still_closes_local_media, reset_wrappers),
		cmocka_unit_test_setup(hidden_restart_still_reconnects_network_media, reset_wrappers),
		cmocka_unit_test_setup(update_with_pending_close_keeps_new_media, reset_wrappers),
	};

	return cmocka_run_group_tests(tests, NULL, NULL);
}
