#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <stdlib.h>
#include <cmocka.h>

#include <obs.h>

/*
 * obs_shutdown() after obs_startup() failed must not crash
 * (obs-rust/obs-studio#1032). A failed obs_startup() shuts the core down
 * itself and leaves no core; a caller that also calls obs_shutdown() on its
 * error path (the media tests do) then dereferenced the freed core. On Linux
 * obs_startup() fails when the X11 hotkey connection cannot be opened, which
 * a display with no X server forces every time.
 */

static void test_shutdown_after_failed_startup(void **state)
{
	UNUSED_PARAMETER(state);

	/* no X server listens on this display */
	setenv("DISPLAY", ":9999", 1);
	unsetenv("WAYLAND_DISPLAY");

	assert_false(obs_startup("en-US", NULL, NULL));
	assert_false(obs_initialized());

	/* the caller's error path also shuts down */
	obs_shutdown();
	assert_false(obs_initialized());
}

int main(void)
{
	const struct CMUnitTest tests[] = {
		cmocka_unit_test(test_shutdown_after_failed_startup),
	};

	return cmocka_run_group_tests(tests, NULL, NULL);
}
