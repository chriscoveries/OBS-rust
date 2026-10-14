/* Regression test for obsproject/obs-studio#10923 (reported by Reiuji-ch):
 * saving a replay uses the filename formatting and the replay buffer
 * filename prefix and suffix from when the replay buffer was started, not
 * the ones set when the replay is saved.
 *
 * The replay buffer output reads its naming settings ("directory",
 * "format", "extension", "allow_spaces") each time it saves, but the
 * frontend only set them when it started the replay buffer. The frontend
 * now applies the current naming before every save with
 * replay_buffer_naming_refresh(), the Qt-free helper this test drives:
 *
 *   - a replay buffer started with one naming, then refreshed with another
 *     (new filename formatting, prefix, suffix, directory, extension,
 *     spaces): its settings must be the new naming;
 *   - the other settings of the output (max_time_sec, max_size_mb) stay as
 *     they were;
 *   - control: replay_buffer_naming_set() gives the settings the replay
 *     buffer starts with.
 *
 * The output is a minimal registered one. Creating an output needs core
 * video, and obs_startup() a display on Linux (run under xvfb-run); the
 * cases skip without one. */
#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <cmocka.h>

#include <stdio.h>
#include <string.h>

#include <obs.h>

#include "replay-buffer-naming.h"

#define OUTPUT_ID "test_replay_buffer_naming_output"

static bool obs_up;

static const char *out_name(void *type_data)
{
	(void)type_data;
	return "replay buffer";
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
	(void)data;
	(void)ts;
}

static const struct replay_buffer_naming started = {
	"/rec/old", "%CCYY-%MM-%DD %hh-%mm-%ss", "Replay", NULL, "mkv", true,
};

static const struct replay_buffer_naming changed = {
	"/rec/new", "%hh-%mm-%ss", "Clip", "end", "mp4", false,
};

static void expect_naming(obs_data_t *settings, const char *dir, const char *format, const char *ext, bool spaces)
{
	assert_string_equal(obs_data_get_string(settings, "directory"), dir);
	assert_string_equal(obs_data_get_string(settings, "format"), format);
	assert_string_equal(obs_data_get_string(settings, "extension"), ext);
	assert_int_equal(obs_data_get_bool(settings, "allow_spaces"), spaces);
}

/* Control: the settings the replay buffer starts with. */
static void start_settings_hold_the_naming(void **state)
{
	(void)state;
	if (!obs_up)
		skip();

	obs_data_t *settings = obs_data_create();
	replay_buffer_naming_set(settings, &started);
	expect_naming(settings, "/rec/old", "Replay %CCYY-%MM-%DD %hh-%mm-%ss", "mkv", true);
	obs_data_release(settings);
}

/* The report: the naming changed while the replay buffer runs. */
static void refresh_applies_the_current_naming(void **state)
{
	(void)state;
	if (!obs_up)
		skip();

	obs_data_t *settings = obs_data_create();
	replay_buffer_naming_set(settings, &started);
	obs_data_set_int(settings, "max_time_sec", 20);
	obs_data_set_int(settings, "max_size_mb", 512);
	obs_output_t *rb = obs_output_create(OUTPUT_ID, "replay buffer", settings, NULL);
	obs_data_release(settings);
	assert_non_null(rb);

	replay_buffer_naming_refresh(rb, &changed);

	settings = obs_output_get_settings(rb);
	fprintf(stderr, "after refresh: directory '%s', format '%s', extension '%s'\n",
		obs_data_get_string(settings, "directory"), obs_data_get_string(settings, "format"),
		obs_data_get_string(settings, "extension"));
	expect_naming(settings, "/rec/new", "Clip %hh-%mm-%ss end", "mp4", false);
	assert_int_equal(obs_data_get_int(settings, "max_time_sec"), 20);
	assert_int_equal(obs_data_get_int(settings, "max_size_mb"), 512);
	obs_data_release(settings);
	obs_output_release(rb);
}

static int group_setup(void **state)
{
	(void)state;
	obs_up = obs_startup("en-US", NULL, NULL);
	if (!obs_up) {
		fprintf(stderr, "skipped: obs_startup failed (no display?)\n");
		return 0;
	}

	struct obs_video_info ovi = {
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
	int ret = obs_reset_video(&ovi);
	if (ret != OBS_VIDEO_SUCCESS) {
		fprintf(stderr, "skipped: obs_reset_video failed: %d\n", ret);
		obs_shutdown();
		obs_up = false;
		return 0;
	}

	struct obs_output_info out = {
		.id = OUTPUT_ID,
		.flags = OBS_OUTPUT_AV | OBS_OUTPUT_ENCODED,
		.get_name = out_name,
		.create = out_create,
		.destroy = out_destroy,
		.start = out_start,
		.stop = out_stop,
	};
	obs_register_output(&out);
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
		cmocka_unit_test(start_settings_hold_the_naming),
		cmocka_unit_test(refresh_applies_the_current_naming),
	};

	return cmocka_run_group_tests(tests, group_setup, group_teardown);
}
