#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <cmocka.h>

#include <nvenc-override.h>

static void test_explicit_off_beats_preset_lookahead(void **state)
{
	(void)state;

	/* The preset would turn lookahead on. The override wrote lookahead=false. */
	assert_false(nvenc_lookahead_enabled(true, true, false));
	assert_true(nvenc_lookahead_force_off(true, false));

	/* No explicit setting: the preset still enables it. */
	assert_true(nvenc_lookahead_enabled(true, false, false));
	assert_false(nvenc_lookahead_force_off(false, false));

	/* Explicit on stays on even when the preset would not. */
	assert_true(nvenc_lookahead_enabled(false, true, true));
	assert_false(nvenc_lookahead_force_off(true, true));

	/* Neither the preset nor the setting asks for it. */
	assert_false(nvenc_lookahead_enabled(false, false, false));
}

static void test_uhq_forces_at_least_four_bframes(void **state)
{
	(void)state;

	assert_int_equal(nvenc_bframes_for_uhq(true, 0), 4);
	assert_int_equal(nvenc_bframes_for_uhq(true, 3), 4);
	assert_int_equal(nvenc_bframes_for_uhq(true, 4), 4);
	assert_int_equal(nvenc_bframes_for_uhq(true, 5), 5);
	assert_int_equal(nvenc_bframes_for_uhq(false, 0), 0);
	assert_int_equal(nvenc_bframes_for_uhq(false, 2), 2);
}

int main(void)
{
	const struct CMUnitTest tests[] = {
		cmocka_unit_test(test_explicit_off_beats_preset_lookahead),
		cmocka_unit_test(test_uhq_forces_at_least_four_bframes),
	};

	return cmocka_run_group_tests(tests, NULL, NULL);
}
