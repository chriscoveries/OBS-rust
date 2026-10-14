/* Regression test for obs-rust/obs-studio#663: test_missing_files_list's first
 * case measures bnum_allocs() right after obs_startup() and was once off by
 * one on Windows. bnum_allocs counts allocations from every thread, so a
 * libobs background thread that still allocates lazily after obs_startup()
 * returns makes the count drift. The count must be stable once obs_startup()
 * returns. obs_startup() needs a display on Linux (run under xvfb-run); the
 * case skips when it cannot start. */

#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <cmocka.h>

#include <obs.h>
#include <util/base.h>
#include <util/bmem.h>
#include <util/platform.h>

static void log_handler(int lvl, const char *msg, va_list args, void *p)
{
	(void)lvl;
	(void)msg;
	(void)args;
	(void)p;
}

static void test_alloc_count_stable_after_startup(void **state)
{
	(void)state;
	base_set_log_handler(log_handler, NULL);

	/* The race is a thread-start race, so repeat to catch it. */
	for (int i = 0; i < 100; i++) {
		if (!obs_startup("en-US", NULL, NULL)) {
			skip();
			return;
		}

		long before = bnum_allocs();
		/* Let the background threads run a few ticks (25 ms each). */
		os_sleep_ms(30);
		long after = bnum_allocs();

		obs_shutdown();
		assert_int_equal(after, before);
	}
}

int main(void)
{
	const struct CMUnitTest tests[] = {
		cmocka_unit_test(test_alloc_count_stable_after_startup),
	};
	return cmocka_run_group_tests(tests, NULL, NULL);
}
