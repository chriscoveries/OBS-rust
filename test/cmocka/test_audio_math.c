#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <math.h>
#include <float.h>
#include <cmocka.h>

#include <util/c99defs.h>
#include <media-io/audio-math.h>

/* Characterization of the header-inline helpers in media-io/audio-math.h.
 *
 * log10f and powf come from the platform libm, and glibc, the UCRT and
 * libSystem can differ in the last bit. Only results every libm gets
 * exactly (zero, infinities, NaN, log10f(1), powf(10, 0), overflow and
 * underflow) are compared exactly; everything else is compared with a
 * relative tolerance. */

static void assert_exact(float actual, float expected)
{
	assert_true(actual == expected);
}

/* About 8 float ULPs. */
static void assert_close(float actual, float expected)
{
	assert_true(fabsf(actual - expected) <= 1e-6f * fabsf(expected));
}

static void test_mul_to_db_exact(void **state)
{
	UNUSED_PARAMETER(state);

	/* 0 and -0 both compare equal to 0, so both give -inf. */
	assert_exact(mul_to_db(0.0f), -INFINITY);
	assert_exact(mul_to_db(-0.0f), -INFINITY);

	assert_exact(mul_to_db(1.0f), 0.0f);
	assert_exact(mul_to_db(INFINITY), INFINITY);
}

static void test_mul_to_db_nan(void **state)
{
	UNUSED_PARAMETER(state);

	/* A negative multiplier has no dB value. Characterized, not endorsed. */
	assert_true(isnan(mul_to_db(-1.0f)));
	assert_true(isnan(mul_to_db(-FLT_MIN)));
	assert_true(isnan(mul_to_db(-INFINITY)));
	assert_true(isnan(mul_to_db(NAN)));
}

static void test_mul_to_db_values(void **state)
{
	UNUSED_PARAMETER(state);

	assert_close(mul_to_db(10.0f), 20.0f);
	assert_close(mul_to_db(0.1f), -20.0f);
	assert_close(mul_to_db(0.5f), -6.0205999f);
	assert_close(mul_to_db(2.0f), 6.0205999f);
	assert_close(mul_to_db(0.001f), -60.0f);

	/* The float extremes, subnormal included, give finite dB. */
	assert_close(mul_to_db(FLT_MIN), -758.59559f);
	assert_close(mul_to_db(FLT_TRUE_MIN), -897.06939f);
	assert_close(mul_to_db(FLT_MAX), 770.63679f);
}

static void test_db_to_mul_exact(void **state)
{
	UNUSED_PARAMETER(state);

	assert_exact(db_to_mul(0.0f), 1.0f);
	assert_exact(db_to_mul(-0.0f), 1.0f);

	/* Non-finite input gives 0, including +inf. Characterized, not
	 * endorsed. */
	assert_exact(db_to_mul(INFINITY), 0.0f);
	assert_exact(db_to_mul(-INFINITY), 0.0f);
	assert_exact(db_to_mul(NAN), 0.0f);
}

static void test_db_to_mul_range(void **state)
{
	UNUSED_PARAMETER(state);

	/* Finite input past float range overflows to inf or underflows to 0. */
	assert_exact(db_to_mul(1000.0f), INFINITY);
	assert_exact(db_to_mul(FLT_MAX), INFINITY);
	assert_exact(db_to_mul(-1000.0f), 0.0f);
	assert_exact(db_to_mul(-FLT_MAX), 0.0f);
}

static void test_db_to_mul_values(void **state)
{
	UNUSED_PARAMETER(state);

	assert_close(db_to_mul(20.0f), 10.0f);
	assert_close(db_to_mul(-20.0f), 0.1f);
	assert_close(db_to_mul(-6.0f), 0.5011872f);
	assert_close(db_to_mul(6.0f), 1.9952623f);
	assert_close(db_to_mul(-60.0f), 0.001f);
}

static void test_round_trip(void **state)
{
	UNUSED_PARAMETER(state);

	assert_close(db_to_mul(mul_to_db(0.25f)), 0.25f);
	assert_close(mul_to_db(db_to_mul(-12.0f)), -12.0f);
	assert_exact(db_to_mul(mul_to_db(0.0f)), 0.0f);
}

int main(void)
{
	const struct CMUnitTest tests[] = {
		cmocka_unit_test(test_mul_to_db_exact),  cmocka_unit_test(test_mul_to_db_nan),
		cmocka_unit_test(test_mul_to_db_values), cmocka_unit_test(test_db_to_mul_exact),
		cmocka_unit_test(test_db_to_mul_range),  cmocka_unit_test(test_db_to_mul_values),
		cmocka_unit_test(test_round_trip),
	};

	return cmocka_run_group_tests(tests, NULL, NULL);
}
