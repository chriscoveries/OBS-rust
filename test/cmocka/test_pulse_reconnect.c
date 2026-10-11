#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <setjmp.h>
#include <cmocka.h>

#include <util/bmem.h>
#include <util/c99defs.h>

#include "pulse-reconnect.h"

/* obs-rust/obs-studio#108: a restarted server must be reconnected to, with
 * backoff, and sources must survive the moment it has no default device. */

static void failure_without_a_server_is_not_retried(void **state)
{
	UNUSED_PARAMETER(state);
	struct pulse_reconnect_state reconnect = {0};
	uint64_t delay = 1;

	assert_false(pulse_reconnect_on_failed(&reconnect, &delay));
	assert_false(reconnect.reconnecting);
}

static void lost_server_is_retried_at_once(void **state)
{
	UNUSED_PARAMETER(state);
	struct pulse_reconnect_state reconnect = {0};
	uint64_t delay = 1;

	/* The first connection is not a reconnection. */
	assert_false(pulse_reconnect_on_ready(&reconnect));
	assert_true(pulse_reconnect_on_failed(&reconnect, &delay));
	assert_int_equal(delay, 0);
	assert_true(reconnect.reconnecting);
}

static void refusing_server_backs_off(void **state)
{
	UNUSED_PARAMETER(state);
	const uint64_t expected[] = {0, 500000, 1000000, 2000000, 4000000, 8000000, 10000000, 10000000};
	struct pulse_reconnect_state reconnect = {0};
	uint64_t delay;

	pulse_reconnect_on_ready(&reconnect);
	for (size_t i = 0; i < sizeof(expected) / sizeof(expected[0]); i++) {
		assert_true(pulse_reconnect_on_failed(&reconnect, &delay));
		assert_int_equal(delay, expected[i]);
	}
}

static void reconnection_notifies_sources_and_resets(void **state)
{
	UNUSED_PARAMETER(state);
	struct pulse_reconnect_state reconnect = {0};
	uint64_t delay;

	pulse_reconnect_on_ready(&reconnect);
	assert_true(pulse_reconnect_on_failed(&reconnect, &delay));
	assert_true(pulse_reconnect_on_failed(&reconnect, &delay));
	assert_true(pulse_reconnect_on_ready(&reconnect));
	assert_false(reconnect.reconnecting);

	assert_true(pulse_reconnect_on_failed(&reconnect, &delay));
	assert_int_equal(delay, 0);
}

static void default_devices(void **state)
{
	UNUSED_PARAMETER(state);
	pa_server_info info = {.default_source_name = "mic", .default_sink_name = "speakers"};
	char *device;

	device = pulse_default_device(&info, true);
	assert_string_equal(device, "mic");
	bfree(device);

	device = pulse_default_device(&info, false);
	assert_string_equal(device, "speakers.monitor");
	bfree(device);
}

static void no_default_device(void **state)
{
	UNUSED_PARAMETER(state);
	/* What a restarted server reports before its devices are back. */
	pa_server_info info = {0};

	assert_null(pulse_default_device(&info, true));
	assert_null(pulse_default_device(&info, false));
}

static void restart_is_retried_then_given_up(void **state)
{
	UNUSED_PARAMETER(state);
	uint32_t attempts = 0;

	for (int i = 1; i < PULSE_RESTART_MAX_ATTEMPTS; i++)
		assert_true(pulse_restart_should_retry(&attempts));
	assert_false(pulse_restart_should_retry(&attempts));
	assert_int_equal(attempts, 0);
}

int main(void)
{
	const struct CMUnitTest tests[] = {
		cmocka_unit_test(failure_without_a_server_is_not_retried),
		cmocka_unit_test(lost_server_is_retried_at_once),
		cmocka_unit_test(refusing_server_backs_off),
		cmocka_unit_test(reconnection_notifies_sources_and_resets),
		cmocka_unit_test(default_devices),
		cmocka_unit_test(no_default_device),
		cmocka_unit_test(restart_is_retried_then_given_up),
	};
	return cmocka_run_group_tests(tests, NULL, NULL);
}
