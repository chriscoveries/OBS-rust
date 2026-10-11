#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <setjmp.h>
#include <errno.h>
#include <cmocka.h>

#include <pipewire/core.h>
#include <util/c99defs.h>

#include "reconnect-policy.h"

/* obs-rust/obs-studio#108: a PipeWire restart must be noticed, and the
 * screencast retried, without reacting to errors that would only repeat. */

static void core_epipe_is_lost_once(void **state)
{
	UNUSED_PARAMETER(state);
	bool disconnected = false;

	assert_true(obs_pipewire_core_error_is_lost(&disconnected, PW_ID_CORE, -EPIPE));
	assert_true(disconnected);
	assert_false(obs_pipewire_core_error_is_lost(&disconnected, PW_ID_CORE, -EPIPE));
}

static void other_core_errors_are_not_lost(void **state)
{
	UNUSED_PARAMETER(state);
	bool disconnected = false;

	assert_false(obs_pipewire_core_error_is_lost(&disconnected, PW_ID_CORE, -ENOENT));
	assert_false(obs_pipewire_core_error_is_lost(&disconnected, 42, -EPIPE));
	assert_false(disconnected);
}

static void stream_error_after_streaming_is_lost(void **state)
{
	UNUSED_PARAMETER(state);
	bool disconnected = false;
	bool streamed = false;

	assert_false(obs_pipewire_stream_state_is_lost(&disconnected, &streamed, PW_STREAM_STATE_CONNECTING));
	assert_false(obs_pipewire_stream_state_is_lost(&disconnected, &streamed, PW_STREAM_STATE_PAUSED));
	assert_false(obs_pipewire_stream_state_is_lost(&disconnected, &streamed, PW_STREAM_STATE_STREAMING));
	assert_true(obs_pipewire_stream_state_is_lost(&disconnected, &streamed, PW_STREAM_STATE_ERROR));
	assert_true(disconnected);
}

static void stream_error_before_streaming_is_not_lost(void **state)
{
	UNUSED_PARAMETER(state);
	bool disconnected = false;
	bool streamed = false;

	/* No matching format or no target node: retrying would fail again. */
	assert_false(obs_pipewire_stream_state_is_lost(&disconnected, &streamed, PW_STREAM_STATE_CONNECTING));
	assert_false(obs_pipewire_stream_state_is_lost(&disconnected, &streamed, PW_STREAM_STATE_ERROR));
	assert_false(disconnected);
}

static void lost_connection_is_reported_once(void **state)
{
	UNUSED_PARAMETER(state);
	bool disconnected = false;
	bool streamed = false;

	assert_true(obs_pipewire_core_error_is_lost(&disconnected, PW_ID_CORE, -EPIPE));
	assert_false(obs_pipewire_stream_state_is_lost(&disconnected, &streamed, PW_STREAM_STATE_STREAMING));
	assert_false(obs_pipewire_stream_state_is_lost(&disconnected, &streamed, PW_STREAM_STATE_ERROR));
}

static void reconnect_backs_off_then_gives_up(void **state)
{
	UNUSED_PARAMETER(state);
	const uint32_t expected[] = {500, 1000, 2000, 4000, 8000, 10000, 10000, 10000, 10000};
	uint32_t attempts = 0;
	uint32_t delay_ms = 0;

	for (size_t i = 0; i < sizeof(expected) / sizeof(expected[0]); i++) {
		assert_true(obs_pipewire_reconnect_next_delay(&attempts, &delay_ms));
		assert_int_equal(delay_ms, expected[i]);
	}
	assert_false(obs_pipewire_reconnect_next_delay(&attempts, &delay_ms));
	assert_int_equal(attempts, OBS_PIPEWIRE_RECONNECT_MAX_ATTEMPTS);
}

int main(void)
{
	const struct CMUnitTest tests[] = {
		cmocka_unit_test(core_epipe_is_lost_once),
		cmocka_unit_test(other_core_errors_are_not_lost),
		cmocka_unit_test(stream_error_after_streaming_is_lost),
		cmocka_unit_test(stream_error_before_streaming_is_not_lost),
		cmocka_unit_test(lost_connection_is_reported_once),
		cmocka_unit_test(reconnect_backs_off_then_gives_up),
	};
	return cmocka_run_group_tests(tests, NULL, NULL);
}
