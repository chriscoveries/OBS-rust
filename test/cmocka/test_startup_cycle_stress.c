/* Regression test for obs-rust/obs-studio#684: obs_startup() waits on a
 * "ready" event that the hotkey thread signals, then destroys it. On macOS the
 * signalling thread can still be inside pthread_mutex_unlock() on the event's
 * mutex when the waiter frees it, a use-after-free that segfaulted
 * intermittently when cycling startup/shutdown. Cycle as fast as possible,
 * with no sleep, to hit that window. obs_startup() needs a display on Linux
 * (run under xvfb-run); the case skips when it cannot start. */

#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <cmocka.h>

#include <obs.h>
#include <util/base.h>

static void log_handler(int lvl, const char *msg, va_list args, void *p)
{
	(void)lvl;
	(void)msg;
	(void)args;
	(void)p;
}

static void test_startup_shutdown_cycles(void **state)
{
	(void)state;
	base_set_log_handler(log_handler, NULL);

	for (int i = 0; i < 2000; i++) {
		if (!obs_startup("en-US", NULL, NULL)) {
			skip();
			return;
		}
		obs_shutdown();
	}
}

int main(void)
{
	const struct CMUnitTest tests[] = {
		cmocka_unit_test(test_startup_shutdown_cycles),
	};
	return cmocka_run_group_tests(tests, NULL, NULL);
}
