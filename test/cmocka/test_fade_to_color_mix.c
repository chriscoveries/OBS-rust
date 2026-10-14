#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <setjmp.h>
#include <math.h>
#include <cmocka.h>

#include "fade-to-color-mix.h"

/* Fade to Color with a switch point of 0% or 100% produced NaN audio gains
 * (obs-rust/obs-studio#488): smoothstep divided by max - min == 0. */

static void check_finite(float sp)
{
	for (int i = 0; i <= 100; i++) {
		float t = (float)i / 100.0f;
		float a = ftc_mix_a(sp, t);
		float b = ftc_mix_b(sp, t);
		assert_false(isnan(a) || isinf(a));
		assert_false(isnan(b) || isinf(b));
		assert_true(a >= 0.0f && a <= 1.0f);
		assert_true(b >= 0.0f && b <= 1.0f);
	}
}

static void test_switch_point_zero_finite(void **state)
{
	(void)state;
	check_finite(0.0f);
}

static void test_switch_point_one_finite(void **state)
{
	(void)state;
	check_finite(1.0f);
}

static void test_switch_point_zero_switches_immediately(void **state)
{
	(void)state;
	/* The outgoing source is already silent and the incoming one fades up. */
	assert_float_equal(ftc_mix_a(0.0f, 0.0f), 0.0f, 1e-6f);
	assert_float_equal(ftc_mix_a(0.0f, 0.5f), 0.0f, 1e-6f);
	assert_float_equal(ftc_mix_b(0.0f, 1.0f), 1.0f, 1e-6f);
}

static void test_switch_point_one_switches_at_end(void **state)
{
	(void)state;
	/* The outgoing source fades out over the whole transition; the
	 * incoming one stays silent until the end. */
	assert_float_equal(ftc_mix_a(1.0f, 0.0f), 1.0f, 1e-6f);
	assert_float_equal(ftc_mix_a(1.0f, 1.0f), 0.0f, 1e-6f);
	assert_float_equal(ftc_mix_b(1.0f, 0.5f), 0.0f, 1e-6f);
	assert_float_equal(ftc_mix_b(1.0f, 1.0f), 1.0f, 1e-6f);
}

static void test_switch_point_half_unchanged(void **state)
{
	(void)state;
	assert_float_equal(ftc_mix_a(0.5f, 0.0f), 1.0f, 1e-6f);
	assert_float_equal(ftc_mix_a(0.5f, 0.5f), 0.0f, 1e-6f);
	assert_float_equal(ftc_mix_b(0.5f, 0.5f), 0.0f, 1e-6f);
	assert_float_equal(ftc_mix_b(0.5f, 1.0f), 1.0f, 1e-6f);
}

int main(void)
{
	const struct CMUnitTest tests[] = {
		cmocka_unit_test(test_switch_point_zero_finite),
		cmocka_unit_test(test_switch_point_one_finite),
		cmocka_unit_test(test_switch_point_zero_switches_immediately),
		cmocka_unit_test(test_switch_point_one_switches_at_end),
		cmocka_unit_test(test_switch_point_half_unchanged),
	};
	return cmocka_run_group_tests(tests, NULL, NULL);
}
