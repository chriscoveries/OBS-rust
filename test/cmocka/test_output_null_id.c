/* Regression test for obsproject/obs-studio#12843 (reported by
 * HanabishiRecca): opening Settings crashed when no registered output
 * supported the stream protocol. The frontend then passed a NULL output
 * id to obs_get_output_supported_video_codecs(), and find_output() ran
 * strcmp() on it.
 *
 * The id-keyed output getters must treat a NULL id like an unknown id
 * (return NULL/0) instead of crashing, as long as at least one output type
 * is registered (with none registered, the lookup loop never runs).
 *
 * Each call runs in a forked child so a crash is reported as a failure
 * instead of aborting the whole test binary. obs_startup() is required to
 * register an output type; it needs a display on Linux (e.g. xvfb-run),
 * and the tests skip when it cannot start.
 */
#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <stdio.h>
#include <cmocka.h>

#ifndef _WIN32
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#endif
#ifdef __linux__
#include <sys/prctl.h>
#endif

#include <obs.h>
#include <obs-module.h>

static bool obs_up;

static const char *dummy_get_name(void *type_data)
{
	UNUSED_PARAMETER(type_data);
	return "Dummy Output";
}

static void *dummy_create(obs_data_t *settings, obs_output_t *output)
{
	UNUSED_PARAMETER(settings);
	UNUSED_PARAMETER(output);
	return NULL;
}

static void dummy_destroy(void *data)
{
	UNUSED_PARAMETER(data);
}

static bool dummy_start(void *data)
{
	UNUSED_PARAMETER(data);
	return false;
}

static void dummy_stop(void *data, uint64_t ts)
{
	UNUSED_PARAMETER(data);
	UNUSED_PARAMETER(ts);
}

static void dummy_encoded_packet(void *data, struct encoder_packet *packet)
{
	UNUSED_PARAMETER(data);
	UNUSED_PARAMETER(packet);
}

static const struct obs_output_info dummy_output = {
	.id = "test_null_id_dummy_output",
	.flags = OBS_OUTPUT_AV | OBS_OUTPUT_ENCODED,
	.encoded_video_codecs = "h264",
	.encoded_audio_codecs = "aac",
	.get_name = dummy_get_name,
	.create = dummy_create,
	.destroy = dummy_destroy,
	.start = dummy_start,
	.stop = dummy_stop,
	.encoded_packet = dummy_encoded_packet,
};

enum null_call {
	CALL_SUPPORTED_VIDEO_CODECS,
	CALL_SUPPORTED_AUDIO_CODECS,
	CALL_OUTPUT_FLAGS,
	CALL_DISPLAY_NAME,
	CALL_DEFAULTS,
	CALL_PROPERTIES,
};

/* Returns 0 when the call returned the "not found" value, 1 otherwise. */
static int run_null_call(enum null_call call)
{
	switch (call) {
	case CALL_SUPPORTED_VIDEO_CODECS:
		return obs_get_output_supported_video_codecs(NULL) != NULL;
	case CALL_SUPPORTED_AUDIO_CODECS:
		return obs_get_output_supported_audio_codecs(NULL) != NULL;
	case CALL_OUTPUT_FLAGS:
		return obs_get_output_flags(NULL) != 0;
	case CALL_DISPLAY_NAME:
		return obs_output_get_display_name(NULL) != NULL;
	case CALL_DEFAULTS:
		return obs_output_defaults(NULL) != NULL;
	case CALL_PROPERTIES:
		return obs_get_output_properties(NULL) != NULL;
	}
	return 1;
}

static void expect_null_id_safe(enum null_call call, const char *name)
{
	if (!obs_up)
		skip();
#ifdef _WIN32
	(void)name;
	assert_int_equal(run_null_call(call), 0);
#else
	fflush(stdout);
	fflush(stderr);
	pid_t pid = fork();
	assert_true(pid >= 0);
	if (pid == 0) {
		/* cmocka traps SIGSEGV and would resume the test runner in the
		 * child; let a crash kill the child instead. */
		signal(SIGSEGV, SIG_DFL);
		signal(SIGBUS, SIG_DFL);
		signal(SIGABRT, SIG_DFL);
#ifdef __linux__
		/* an expected crash should not leave a core dump behind */
		prctl(PR_SET_DUMPABLE, 0);
#endif
		_exit(run_null_call(call));
	}

	int status = 0;
	assert_int_equal(waitpid(pid, &status, 0), pid);
	if (WIFSIGNALED(status)) {
		fprintf(stderr, "%s(NULL) crashed with signal %d\n", name, WTERMSIG(status));
		fail();
	}
	if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
		fprintf(stderr, "%s(NULL) did not return the not-found value\n", name);
		fail();
	}
#endif
}

static void registered_output_still_found(void **state)
{
	UNUSED_PARAMETER(state);
	if (!obs_up)
		skip();
	const char *vcodecs = obs_get_output_supported_video_codecs(dummy_output.id);
	assert_non_null(vcodecs);
	assert_string_equal(vcodecs, "h264");
	assert_string_equal(obs_output_get_display_name(dummy_output.id), "Dummy Output");
}

static void supported_video_codecs_null_id(void **state)
{
	UNUSED_PARAMETER(state);
	expect_null_id_safe(CALL_SUPPORTED_VIDEO_CODECS, "obs_get_output_supported_video_codecs");
}

static void supported_audio_codecs_null_id(void **state)
{
	UNUSED_PARAMETER(state);
	expect_null_id_safe(CALL_SUPPORTED_AUDIO_CODECS, "obs_get_output_supported_audio_codecs");
}

static void output_flags_null_id(void **state)
{
	UNUSED_PARAMETER(state);
	expect_null_id_safe(CALL_OUTPUT_FLAGS, "obs_get_output_flags");
}

static void display_name_null_id(void **state)
{
	UNUSED_PARAMETER(state);
	expect_null_id_safe(CALL_DISPLAY_NAME, "obs_output_get_display_name");
}

static void defaults_null_id(void **state)
{
	UNUSED_PARAMETER(state);
	expect_null_id_safe(CALL_DEFAULTS, "obs_output_defaults");
}

static void properties_null_id(void **state)
{
	UNUSED_PARAMETER(state);
	expect_null_id_safe(CALL_PROPERTIES, "obs_get_output_properties");
}

static int group_setup(void **state)
{
	UNUSED_PARAMETER(state);
	obs_up = obs_startup("en-US", NULL, NULL);
	if (obs_up)
		obs_register_output(&dummy_output);
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
		cmocka_unit_test(registered_output_still_found),
		cmocka_unit_test(supported_video_codecs_null_id),
		cmocka_unit_test(supported_audio_codecs_null_id),
		cmocka_unit_test(output_flags_null_id),
		cmocka_unit_test(display_name_null_id),
		cmocka_unit_test(defaults_null_id),
		cmocka_unit_test(properties_null_id),
	};
	return cmocka_run_group_tests(tests, group_setup, group_teardown);
}
