#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <string.h>
#include <stdlib.h>
#include <cmocka.h>

#include <util/platform.h>
#include <util/bmem.h>

/*
 * Characterization tests for the POSIX time/sleep and directory helpers in
 * libobs/util/platform-nix-time-dir.c (extracted from platform-nix.c ahead of
 * the Rust port; os_opendir/os_readdir/os_closedir are additionally covered
 * by test_platform.c).
 *
 * Everything lives under a temp dir relative to cwd, with '/' separators.
 * Sleep bounds are one-sided lower bounds only: the POSIX calls never return
 * before the requested interval, but a loaded host may always return later.
 */

#define ROOT "test_platform_time_dir.tmp"

#define D_GLOB ROOT "/glob"
#define D_GLOB_SUB ROOT "/glob/sub"
#define P_GLOB_A ROOT "/glob/a.txt"
#define P_GLOB_B ROOT "/glob/b.txt"
#define P_GLOB_C ROOT "/glob/c.bin"
#define D_GLOB2 ROOT "/glob2"
#define P_GLOB2_A ROOT "/glob2/a.txt"

static int group_setup(void **state)
{
	UNUSED_PARAMETER(state);

	os_mkdirs(ROOT);
	return 0;
}

static int group_teardown(void **state)
{
	UNUSED_PARAMETER(state);

	os_unlink(P_GLOB_A);
	os_unlink(P_GLOB_B);
	os_unlink(P_GLOB_C);
	os_unlink(P_GLOB2_A);
	os_rmdir(D_GLOB_SUB);
	os_rmdir(D_GLOB);
	os_rmdir(D_GLOB2);
	os_rmdir(ROOT);
	return 0;
}

static bool write_str(const char *path, const char *str)
{
	return os_quick_write_utf8_file(path, str, strlen(str), false);
}

static void test_gettime_ns_monotonic(void **state)
{
	UNUSED_PARAMETER(state);

	uint64_t t1 = os_gettime_ns();
	uint64_t t2 = os_gettime_ns();

	/* CLOCK_MONOTONIC never goes backwards */
	assert_true(t2 >= t1);

	os_sleep_ms(2);
	t2 = os_gettime_ns();
	assert_true(t2 > t1);
}

static void test_sleep_ms_waits(void **state)
{
	UNUSED_PARAMETER(state);

	uint64_t t0 = os_gettime_ns();
	os_sleep_ms(50);
	uint64_t elapsed = os_gettime_ns() - t0;

	/* nanosleep may not return early without a signal (characterized) */
	assert_true(elapsed >= 45 * 1000 * 1000);
}

static void test_sleep_ms_zero(void **state)
{
	UNUSED_PARAMETER(state);

	uint64_t t0 = os_gettime_ns();
	os_sleep_ms(0);
	uint64_t elapsed = os_gettime_ns() - t0;

	/* usleep(0) is a no-op */
	assert_true(elapsed < 1000 * 1000 * 1000);
}

static void test_sleepto_ns_past_is_false(void **state)
{
	UNUSED_PARAMETER(state);

	uint64_t now = os_gettime_ns();

	assert_false(os_sleepto_ns(now - 1000 * 1000 * 1000));
	assert_false(os_sleepto_ns_fast(now - 1000 * 1000 * 1000));
	/* characterized, not endorsed: a target of exactly now is "past or
	 * equal", so both return false without sleeping */
	assert_false(os_sleepto_ns(os_gettime_ns()));
	assert_false(os_sleepto_ns_fast(os_gettime_ns()));
}

static void test_sleepto_ns_future_waits(void **state)
{
	UNUSED_PARAMETER(state);

	uint64_t t0 = os_gettime_ns();
	assert_true(os_sleepto_ns(t0 + 40 * 1000 * 1000));
	uint64_t elapsed = os_gettime_ns() - t0;
	assert_true(elapsed >= 35 * 1000 * 1000);

	t0 = os_gettime_ns();
	assert_true(os_sleepto_ns_fast(t0 + 40 * 1000 * 1000));
	elapsed = os_gettime_ns() - t0;
	assert_true(elapsed >= 35 * 1000 * 1000);
}

static void test_glob_matches_and_flags(void **state)
{
	UNUSED_PARAMETER(state);

	assert_int_equal(os_mkdir(D_GLOB), MKDIR_SUCCESS);
	assert_int_equal(os_mkdir(D_GLOB_SUB), MKDIR_SUCCESS);
	assert_true(write_str(P_GLOB_A, "a"));
	assert_true(write_str(P_GLOB_B, "b"));
	assert_true(write_str(P_GLOB_C, "c"));

	os_glob_t *g = NULL;
	int ret = os_glob(ROOT "/glob/*.txt", 0, &g);
	assert_int_equal(ret, 0);
	assert_non_null(g);
	assert_int_equal(g->gl_pathc, 2);

	/* glob(3) sorts matches; subdirectory entries flag directory=true */
	bool saw_a = false, saw_b = false;
	for (size_t i = 0; i < g->gl_pathc; i++) {
		const char *p = g->gl_pathv[i].path;
		if (strstr(p, "a.txt"))
			saw_a = true;
		if (strstr(p, "b.txt"))
			saw_b = true;
		assert_false(g->gl_pathv[i].directory);
	}
	assert_true(saw_a && saw_b);
	os_globfree(g);

	g = NULL;
	ret = os_glob(ROOT "/glob/*", 0, &g);
	assert_int_equal(ret, 0);
	assert_non_null(g);
	assert_int_equal(g->gl_pathc, 4);

	bool saw_sub = false;
	for (size_t i = 0; i < g->gl_pathc; i++) {
		const char *p = g->gl_pathv[i].path;
		bool is_sub = strstr(p, "sub") != NULL;
		if (is_sub) {
			saw_sub = true;
			assert_true(g->gl_pathv[i].directory);
		} else {
			assert_false(g->gl_pathv[i].directory);
		}
	}
	assert_true(saw_sub);
	os_globfree(g);
}

static void test_glob_flags_ignored(void **state)
{
	UNUSED_PARAMETER(state);

	/* own fixture dir: fixtures are only removed in group_teardown, and
	 * test_glob_matches_and_flags leaves D_GLOB populated */
	assert_int_equal(os_mkdir(D_GLOB2), MKDIR_SUCCESS);
	assert_true(write_str(P_GLOB2_A, "a"));

	/* characterized, not endorsed: the flags argument is ignored entirely,
	 * the call always passes 0 to glob(3) */
	os_glob_t *g = NULL;
	int ret = os_glob(ROOT "/glob2/*.txt", 999, &g);
	assert_int_equal(ret, 0);
	assert_non_null(g);
	assert_int_equal(g->gl_pathc, 1);
	os_globfree(g);
}

static void test_glob_no_match(void **state)
{
	UNUSED_PARAMETER(state);

	os_glob_t *g = NULL;
	int ret = os_glob(ROOT "/glob/no_such_*.zzz", 0, &g);
	assert_int_not_equal(ret, 0);
	assert_null(g);

	/* NULL globfree is tolerated */
	os_globfree(NULL);
}

int main(void)
{
	const struct CMUnitTest tests[] = {
		cmocka_unit_test(test_gettime_ns_monotonic),    cmocka_unit_test(test_sleep_ms_waits),
		cmocka_unit_test(test_sleep_ms_zero),           cmocka_unit_test(test_sleepto_ns_past_is_false),
		cmocka_unit_test(test_sleepto_ns_future_waits), cmocka_unit_test(test_glob_matches_and_flags),
		cmocka_unit_test(test_glob_flags_ignored),      cmocka_unit_test(test_glob_no_match),
	};

	return cmocka_run_group_tests(tests, group_setup, group_teardown);
}
