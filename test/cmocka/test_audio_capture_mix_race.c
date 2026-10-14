/* #130: real capture packets race with the real mixer. Run with libobs and
 * this executable built with -fsanitize=thread to detect unlocked timestamp
 * and deque reads. Events pace capture from mixer callbacks; no sleeps or
 * unsynchronized test state are used. */
#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <cmocka.h>
#include <stdio.h>
#include <stdlib.h>

#include <obs.h>
#include <util/platform.h>
#include <util/threading.h>

#define RATE 48000
#define FRAMES 1024
#define TICKS 12
#define PACKETS_PER_TICK 5000

/* Written only by the mixer and read after obs_shutdown joins it. */
static size_t transition_renders;
static size_t transition_successes;
static size_t transition_mix_calls;

struct capture {
	obs_source_t *source;
	os_sem_t *tick;
	bool failed;
	size_t packets;
	bool pace_from_transition;
};

static struct capture *transition_capture;

static const char *capture_name(void *unused)
{
	(void)unused;
	return "concurrent capture";
}

static void *capture_create(obs_data_t *settings, obs_source_t *source)
{
	(void)settings;
	return source;
}

static void capture_destroy(void *data)
{
	(void)data;
}

static void quiet_log(int level, const char *format, va_list args, void *param)
{
	(void)param;
	if (level <= LOG_WARNING) {
		vfprintf(stderr, format, args);
		fputc('\n', stderr);
	}
}

static void mixed_audio(void *param, size_t mix, struct audio_data *audio)
{
	(void)mix;
	(void)audio;
	struct capture *capture = param;
	if (!capture->pace_from_transition)
		os_sem_post(capture->tick);
}

static void *produce_audio(void *param)
{
	struct capture *capture = param;
	float samples[FRAMES];
	for (size_t i = 0; i < FRAMES; i++)
		samples[i] = 0.25f;
	struct obs_source_audio audio = {
		.data = {(const uint8_t *)samples, (const uint8_t *)samples},
		.frames = FRAMES,
		.speakers = SPEAKERS_STEREO,
		.format = AUDIO_FORMAT_FLOAT_PLANAR,
		.samples_per_sec = RATE,
	};
	for (size_t tick = 0; tick < TICKS; tick++) {
		if (os_sem_wait(capture->tick) != 0) {
			capture->failed = true;
			break;
		}
		for (size_t packet = 0; packet < PACKETS_PER_TICK; packet++) {
			/* Discontinuities exercise reset_audio_data as well as deque
			 * growth while the mixer renders and checks buffering. */
			audio.timestamp = os_gettime_ns() + (packet & 1 ? 200000000ULL : 0);
			obs_source_output_audio(capture->source, &audio);
			capture->packets++;
		}
	}
	return NULL;
}

static float transition_mix(void *data, float t)
{
	(void)data;
	(void)t;
	transition_mix_calls++;
	return 1.0f;
}

static bool transition_render(void *data, uint64_t *timestamp, struct obs_source_audio_mix *audio, uint32_t mixers,
			      size_t channels, size_t sample_rate)
{
	transition_renders++;
	os_sem_post(transition_capture->tick);
	bool success = obs_transition_audio_render(data, timestamp, audio, mixers, channels, sample_rate,
						   transition_mix, transition_mix);
	transition_successes += success;
	return success;
}

static void transition_video_render(void *data, gs_effect_t *effect)
{
	(void)effect;
	obs_transition_video_render(data, NULL);
}

static void run_capture(bool through_transition)
{
	base_set_log_handler(quiet_log, NULL);
	transition_renders = 0;
	transition_successes = 0;
	transition_mix_calls = 0;
	if (!getenv("DISPLAY"))
		skip();
	assert_true(obs_startup("en-US", NULL, NULL));
	struct obs_video_info video = {
		.graphics_module = GRAPHICS_MODULE_PATH,
		.fps_num = 30,
		.fps_den = 1,
		.base_width = 64,
		.base_height = 64,
		.output_width = 64,
		.output_height = 64,
		.output_format = VIDEO_FORMAT_NV12,
		.colorspace = VIDEO_CS_709,
		.range = VIDEO_RANGE_PARTIAL,
		.scale_type = OBS_SCALE_BICUBIC,
	};
	int video_result = obs_reset_video(&video);
	if (video_result != OBS_VIDEO_SUCCESS) {
		obs_shutdown();
		assert_int_equal(video_result, OBS_VIDEO_SUCCESS);
	}
	struct obs_audio_info audio = {.samples_per_sec = RATE, .speakers = SPEAKERS_STEREO};
	assert_true(obs_reset_audio(&audio));
	struct obs_source_info info = {
		.id = "test_audio_capture_mix_race",
		.type = OBS_SOURCE_TYPE_INPUT,
		.output_flags = OBS_SOURCE_AUDIO,
		.get_name = capture_name,
		.create = capture_create,
		.destroy = capture_destroy,
	};
	obs_register_source(&info);
	struct capture capture = {.pace_from_transition = through_transition};
	assert_int_equal(os_sem_init(&capture.tick, 0), 0);
	capture.source = obs_source_create_private(info.id, "concurrent capture", NULL);
	assert_non_null(capture.source);
	obs_source_t *transition = NULL;
	obs_source_t *second = NULL;
	if (through_transition) {
		transition_capture = &capture;
		struct obs_source_info transition_info = {
			.id = "test_audio_capture_transition",
			.type = OBS_SOURCE_TYPE_TRANSITION,
			.output_flags = OBS_SOURCE_VIDEO,
			.get_name = capture_name,
			.create = capture_create,
			.destroy = capture_destroy,
			.audio_render = transition_render,
			.video_render = transition_video_render,
		};
		obs_register_source(&transition_info);
		transition = obs_source_create_private(transition_info.id, "capture transition", NULL);
		assert_non_null(transition);
		second = obs_source_create_private(info.id, "second capture", NULL);
		assert_non_null(second);
		float samples[FRAMES];
		for (size_t i = 0; i < FRAMES; i++)
			samples[i] = 0.25f;
		struct obs_source_audio seed = {
			.data = {(const uint8_t *)samples, (const uint8_t *)samples},
			.frames = FRAMES,
			.speakers = SPEAKERS_STEREO,
			.format = AUDIO_FORMAT_FLOAT_PLANAR,
			.samples_per_sec = RATE,
			.timestamp = os_gettime_ns(),
		};
		for (size_t i = 0; i < 8; i++) {
			obs_source_output_audio(capture.source, &seed);
			obs_source_output_audio(second, &seed);
			seed.timestamp += 1000000000ULL * FRAMES / RATE;
		}
		obs_transition_set(transition, capture.source);
		assert_true(obs_transition_start(transition, OBS_TRANSITION_MODE_AUTO, 60000, second));
	}
	obs_set_output_source(0, transition ? transition : capture.source);
	obs_add_raw_audio_callback(0, NULL, mixed_audio, &capture);
	pthread_t producer;
	assert_int_equal(pthread_create(&producer, NULL, produce_audio, &capture), 0);
	assert_int_equal(pthread_join(producer, NULL), 0);
	obs_remove_raw_audio_callback(0, mixed_audio, &capture);
	obs_set_output_source(0, NULL);
	obs_source_release(transition);
	obs_source_release(second);
	obs_source_release(capture.source);
	obs_shutdown();
	os_sem_destroy(capture.tick);
	assert_false(capture.failed);
	assert_int_equal(capture.packets, TICKS * PACKETS_PER_TICK);
	if (through_transition) {
		assert_true(transition_renders > 0);
		assert_true(transition_successes > 0);
		assert_true(transition_mix_calls > 0);
	}
}

static void capture_and_mix(void **state)
{
	(void)state;
	run_capture(false);
}

static void capture_under_transition(void **state)
{
	(void)state;
	run_capture(true);
}

int main(void)
{
	const struct CMUnitTest tests[] = {cmocka_unit_test(capture_and_mix),
					   cmocka_unit_test(capture_under_transition)};
	return cmocka_run_group_tests(tests, NULL, NULL);
}
