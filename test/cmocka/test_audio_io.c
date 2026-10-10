/* Characterization test for libobs/media-io/audio-io.c.
 *
 * Pins the CURRENT C behavior of the audio_output API ahead of the
 * obs-media-io port (Phase 5). Characterized, not endorsed: where the C
 * code crashes or misbehaves, the test pins that rather than the ideal.
 *
 * audio_output_open() spawns the input thread immediately, and that
 * thread dereferences obs->name_store on entry, so the unit is only
 * exercisable with a running obs core (obs_startup). obs_startup needs
 * a display on Linux, so the behavioral pins run under `xvfb-run` and
 * skip otherwise; the parameter-validation and crash pins run anywhere.
 */
#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <cmocka.h>

#ifndef _WIN32
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

#include <obs.h>
#include <media-io/audio-io.h>
#include <util/platform.h>
#include <util/threading.h>

static int obs_up;
static volatile size_t g_input_calls;
static os_event_t *g_input_event;

static bool counting_input(void *param, uint64_t start_ts, uint64_t end_ts, uint64_t *new_ts, uint32_t active_mixers,
			   struct audio_output_data *mixes)
{
	size_t *count = param;
	(void)start_ts;
	(void)end_ts;
	(void)new_ts;
	(void)active_mixers;
	(void)mixes;
	if (count)
		(*count)++;
	else
		g_input_calls++;
	if (g_input_event)
		os_event_signal(g_input_event);
	return true;
}

static void silent_output(void *param, size_t mix_idx, struct audio_data *data)
{
	uint32_t *frames = param;
	(void)mix_idx;
	/* param may be NULL: connect() accepts it, and this callback still
	 * runs on the audio thread for that input. */
	if (frames)
		*frames += data->frames;
}

static struct audio_output_info stereo_info(void)
{
	/* input_callback is effectively required: the audio thread calls
	 * it unconditionally on the first tick (pinned below). */
	struct audio_output_info info = {
		.name = "t4-audio",
		.samples_per_sec = 48000,
		.format = AUDIO_FORMAT_FLOAT,
		.speakers = SPEAKERS_STEREO,
		.input_callback = counting_input,
		.input_param = NULL,
	};
	return info;
}

/* open() must fail AUDIO_OUTPUT_INVALIDPARAM when any of format, name,
 * samples_per_sec or speakers is unset, and must not touch *audio. */
static void open_invalid_params(void **state)
{
	UNUSED_PARAMETER(state);

	struct {
		const char *name;
		uint32_t samples_per_sec;
		enum audio_format format;
		enum speaker_layout speakers;
	} bads[] = {
		{"x", 48000, AUDIO_FORMAT_UNKNOWN, SPEAKERS_STEREO},
		{NULL, 48000, AUDIO_FORMAT_FLOAT, SPEAKERS_STEREO},
		{"x", 0, AUDIO_FORMAT_FLOAT, SPEAKERS_UNKNOWN},
		{"x", 48000, AUDIO_FORMAT_FLOAT, SPEAKERS_UNKNOWN},
	};

	for (size_t i = 0; i < sizeof(bads) / sizeof(bads[0]); i++) {
		audio_t *audio = (audio_t *)(size_t)0xdeadbeef;
		struct audio_output_info info = {
			.name = bads[i].name,
			.samples_per_sec = bads[i].samples_per_sec,
			.format = bads[i].format,
			.speakers = bads[i].speakers,
		};
		assert_int_equal(audio_output_open(&audio, &info), AUDIO_OUTPUT_INVALIDPARAM);
		assert_ptr_equal(audio, (audio_t *)(size_t)0xdeadbeef);
	}
}

/* Interleaved float stereo: planes 1, channels 2, block 2*4. */
static void open_getters_stereo(void **state)
{
	UNUSED_PARAMETER(state);
	if (!obs_up)
		skip();

	audio_t *audio = NULL;
	struct audio_output_info info = stereo_info();
	assert_int_equal(audio_output_open(&audio, &info), AUDIO_OUTPUT_SUCCESS);
	assert_non_null(audio);

	const struct audio_output_info *got = audio_output_get_info(audio);
	assert_non_null(got);
	assert_string_equal(got->name, "t4-audio");
	assert_int_equal(got->samples_per_sec, 48000);
	assert_int_equal(got->format, AUDIO_FORMAT_FLOAT);
	assert_int_equal(got->speakers, SPEAKERS_STEREO);
	assert_ptr_equal(got->input_callback, counting_input);

	assert_int_equal(audio_output_get_sample_rate(audio), 48000);
	assert_int_equal(audio_output_get_channels(audio), 2);
	assert_int_equal(audio_output_get_planes(audio), 1);
	assert_int_equal(audio_output_get_block_size(audio), 8);
	assert_false(audio_output_active(audio));

	audio_output_close(audio);
}

/* Planar formats put one buffer per channel; block size is per plane. */
static void open_getters_planar(void **state)
{
	UNUSED_PARAMETER(state);
	if (!obs_up)
		skip();

	audio_t *audio = NULL;
	struct audio_output_info info = stereo_info();
	info.format = AUDIO_FORMAT_16BIT_PLANAR;
	info.speakers = SPEAKERS_5POINT1;
	assert_int_equal(audio_output_open(&audio, &info), AUDIO_OUTPUT_SUCCESS);
	assert_non_null(audio);

	assert_int_equal(audio_output_get_channels(audio), 6);
	assert_int_equal(audio_output_get_planes(audio), 6);
	assert_int_equal(audio_output_get_block_size(audio), 2);

	audio_output_close(audio);
}

/* connect(): NULL or partially-zero conversion falls back to the output
 * info; duplicate (callback, param) on the same mix is refused; a mix
 * index >= MAX_AUDIO_MIXES and a NULL audio are refused. */
static void connect_disconnect(void **state)
{
	UNUSED_PARAMETER(state);
	if (!obs_up)
		skip();

	audio_t *audio = NULL;
	struct audio_output_info info = stereo_info();
	assert_int_equal(audio_output_open(&audio, &info), AUDIO_OUTPUT_SUCCESS);

	assert_false(audio_output_active(audio));
	assert_true(audio_output_connect(audio, 0, NULL, silent_output, NULL));
	assert_true(audio_output_active(audio));

	/* duplicate (callback, param) on the same mix is refused */
	uint32_t frames = 0;
	assert_false(audio_output_connect(audio, 0, NULL, silent_output, NULL));
	/* same callback with a different param is a new input */
	assert_true(audio_output_connect(audio, 0, NULL, silent_output, &frames));
	/* a different mix is fine */
	assert_true(audio_output_connect(audio, 1, NULL, silent_output, &frames));

	/* out of bounds / NULL */
	assert_false(audio_output_connect(audio, MAX_AUDIO_MIXES, NULL, silent_output, NULL));
	assert_false(audio_output_connect(NULL, 0, NULL, silent_output, NULL));

	audio_output_disconnect(audio, 0, silent_output, NULL);
	assert_true(audio_output_active(audio)); /* &frames inputs live */
	audio_output_disconnect(audio, 0, silent_output, &frames);
	assert_true(audio_output_active(audio)); /* mix 1 still live */
	audio_output_disconnect(audio, 1, silent_output, &frames);
	assert_false(audio_output_active(audio));

	/* unknown fields in conversion fall back to output settings */
	struct audio_convert_info conv = {
		.samples_per_sec = 0,
		.format = AUDIO_FORMAT_UNKNOWN,
		.speakers = SPEAKERS_UNKNOWN,
		.allow_clipping = false,
	};
	assert_true(audio_output_connect(audio, 0, &conv, silent_output, NULL));
	audio_output_disconnect(audio, 0, silent_output, NULL);

	/* disconnecting an input the mix does not have is a noop */
	audio_output_disconnect(audio, 5, silent_output, (void *)(size_t)0x42);

	audio_output_disconnect(audio, 0, silent_output, NULL);               /* noop */
	audio_output_disconnect(audio, MAX_AUDIO_MIXES, silent_output, NULL); /* noop */
	audio_output_disconnect(NULL, 0, silent_output, NULL);                /* noop */

	audio_output_close(audio);
}

/* NULL-only getters: get_info and active guard NULL, the rest do not. */
static void null_surface(void **state)
{
	UNUSED_PARAMETER(state);

	assert_null(audio_output_get_info(NULL));
	assert_false(audio_output_active(NULL));

	audio_output_close(NULL);
}

/* unguarded NULL getters crash (characterized, not endorsed). */
static void null_getters_crash(void **state)
{
	UNUSED_PARAMETER(state);

#ifdef _WIN32
	skip();
#else
	pid_t pid = fork();
	assert_true(pid >= 0);
	if (pid == 0) {
		/* reset the handlers cmocka installs in the test process so
		 * the fault kills us instead of being caught */
		signal(SIGSEGV, SIG_DFL);
		signal(SIGBUS, SIG_DFL);
		signal(SIGABRT, SIG_DFL);
		audio_output_get_block_size(NULL);
		_exit(0);
	}
	int status = 0;
	assert_int_equal(waitpid(pid, &status, 0), pid);
	assert_true(WIFSIGNALED(status));
#endif
}

/* open() spawns the input thread immediately: a registered input callback
 * is invoked without any other action, and stops at close. */
static void input_thread_runs(void **state)
{
	UNUSED_PARAMETER(state);
	if (!obs_up)
		skip();

	volatile size_t calls = 0;
	audio_t *audio = NULL;
	struct audio_output_info info = stereo_info();
	info.input_param = (void *)&calls;
	assert_int_equal(os_event_init(&g_input_event, OS_EVENT_TYPE_AUTO), 0);
	assert_int_equal(audio_output_open(&audio, &info), AUDIO_OUTPUT_SUCCESS);

	assert_int_equal(os_event_timedwait(g_input_event, 3000), 0);
	os_event_destroy(g_input_event);
	g_input_event = NULL;
	assert_true(calls > 0);

	audio_output_close(audio);
}

/* Without a running obs core the audio thread dereferences
 * obs->name_store on entry and the process dies right after open.
 * Characterized, not endorsed. POSIX only. */
static void open_crashes_without_obs_core(void **state)
{
	UNUSED_PARAMETER(state);

#ifdef _WIN32
	skip();
#else
	if (obs_up)
		skip();

	pid_t pid = fork();
	assert_true(pid >= 0);
	if (pid == 0) {
		signal(SIGSEGV, SIG_DFL);
		signal(SIGBUS, SIG_DFL);
		signal(SIGABRT, SIG_DFL);
		audio_t *audio = NULL;
		struct audio_output_info info = stereo_info();
		assert_int_equal(audio_output_open(&audio, &info), AUDIO_OUTPUT_SUCCESS);
		os_sleep_ms(500);
		audio_output_close(audio);
		/* if this line runs the crash did not happen */
		_exit(0);
	}
	int status = 0;
	assert_int_equal(waitpid(pid, &status, 0), pid);
	assert_true(WIFSIGNALED(status));
#endif
}

static int group_setup(void **state)
{
	UNUSED_PARAMETER(state);
	obs_up = obs_startup("en-US", NULL, NULL);
	return 0;
}

static int group_teardown(void **state)
{
	UNUSED_PARAMETER(state);
	if (obs_up)
		obs_shutdown();
	return 0;
}

int main(void)
{
	const struct CMUnitTest tests[] = {
		/* environment-independent pins */
		cmocka_unit_test(open_invalid_params),
		cmocka_unit_test(null_surface),
		cmocka_unit_test(null_getters_crash),
		/* only meaningful while the obs core is absent */
		cmocka_unit_test(open_crashes_without_obs_core),
		/* behavioral pins: need obs_startup (a display on Linux) */
		cmocka_unit_test(open_getters_stereo),
		cmocka_unit_test(open_getters_planar),
		cmocka_unit_test(connect_disconnect),
		cmocka_unit_test(input_thread_runs),
	};
	return cmocka_run_group_tests(tests, group_setup, group_teardown);
}
