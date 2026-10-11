#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <cmocka.h>

#include <util/c99defs.h>
#include <util/threading.h>

#include "mac-sck-stop.h"

/* The ScreenCaptureKit destroy paths must not free the stop event while
 * its completion block can still run: on timeout the event is leaked and
 * the rest of the teardown proceeds. The wait helper is driven by real
 * os_event objects: an unsignalled event is the never-fires case, a
 * signalled event the completed case, NULL the event-init failure. */

static void test_wait_never_fires_times_out(void **state)
{
	UNUSED_PARAMETER(state);
	os_event_t *ev = NULL;
	assert_int_equal(os_event_init(&ev, OS_EVENT_TYPE_AUTO), 0);
	assert_non_null(ev);
	assert_int_equal(sck_wait_for_stop(ev, 25), SCK_STOP_WAIT_TIMEOUT);
	os_event_destroy(ev);
}

static void test_wait_signalled_completes(void **state)
{
	UNUSED_PARAMETER(state);
	os_event_t *ev = NULL;
	assert_int_equal(os_event_init(&ev, OS_EVENT_TYPE_AUTO), 0);
	os_event_signal(ev);
	assert_int_equal(sck_wait_for_stop(ev, 2000), SCK_STOP_WAIT_COMPLETED);
	os_event_destroy(ev);
}

static void test_wait_null_event_is_error(void **state)
{
	UNUSED_PARAMETER(state);
	assert_int_equal(sck_wait_for_stop(NULL, 25), SCK_STOP_WAIT_ERROR);
}

static void test_free_decision(void **state)
{
	UNUSED_PARAMETER(state);
	assert_true(sck_stop_may_destroy_event(SCK_STOP_WAIT_COMPLETED));
	assert_true(sck_stop_may_destroy_event(SCK_STOP_WAIT_ERROR));
	assert_false(sck_stop_may_destroy_event(SCK_STOP_WAIT_TIMEOUT));
}

int main(void)
{
	const struct CMUnitTest tests[] = {
		cmocka_unit_test(test_wait_never_fires_times_out),
		cmocka_unit_test(test_wait_signalled_completes),
		cmocka_unit_test(test_wait_null_event_is_error),
		cmocka_unit_test(test_free_decision),
	};
	return cmocka_run_group_tests(tests, NULL, NULL);
}
