#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <cmocka.h>

/* util_uint64.h uses uint64_t without including <stdint.h>. */
#include <util/c99defs.h>
#include <util/util_uint64.h>

/* util_uint64.h picks the exact 128-bit path under the same condition. */
#if defined(_MSC_VER) && defined(_M_X64) && (_MSC_VER >= 1920)
#define UTIL_MUL_DIV64_EXACT 1
#else
#define UTIL_MUL_DIV64_EXACT 0
#endif

static void test_audio_frames_to_ns(void **state)
{
	UNUSED_PARAMETER(state);

	/* audio_frames_to_ns(48000, 1024) and friends */
	assert_int_equal(util_mul_div64(1024, 1000000000ULL, 48000), 21333333ULL);
	assert_int_equal(util_mul_div64(48000, 1000000000ULL, 48000), 1000000000ULL);
	assert_int_equal(util_mul_div64(1, 1000000000ULL, 44100), 22675ULL);
}

static void test_ns_to_audio_frames(void **state)
{
	UNUSED_PARAMETER(state);

	assert_int_equal(util_mul_div64(1000000000ULL, 48000, 1000000000ULL), 48000ULL);
	assert_int_equal(util_mul_div64(1000000000000000000ULL, 48000, 1000000000ULL), 48000000000000ULL);
}

static void test_frame_interval(void **state)
{
	UNUSED_PARAMETER(state);

	/* 30000/1001 fps: interval in ns, truncated */
	assert_int_equal(util_mul_div64(1001, 1000000000ULL, 30000), 33366666ULL);
}

static void test_zero_operands(void **state)
{
	UNUSED_PARAMETER(state);

	assert_int_equal(util_mul_div64(0, 123, 7), 0ULL);
	assert_int_equal(util_mul_div64(123, 0, 7), 0ULL);
}

static void test_div_one(void **state)
{
	UNUSED_PARAMETER(state);

	assert_int_equal(util_mul_div64(7, 1, 1), 7ULL);
	assert_int_equal(util_mul_div64(UINT64_MAX, 1, 1), UINT64_MAX);
}

static void test_large_values_without_overflow(void **state)
{
	UNUSED_PARAMETER(state);

	/* num * mul exceeds 64 bits, but rem * mul does not, so both paths
	 * give the exact quotient. */
	assert_int_equal(util_mul_div64(UINT64_MAX, UINT64_MAX, UINT64_MAX), UINT64_MAX);
	assert_int_equal(util_mul_div64(UINT64_MAX, 3, 3), UINT64_MAX);
	assert_int_equal(util_mul_div64(UINT64_MAX, 2, 4), 0x7FFFFFFFFFFFFFFFULL);
	assert_int_equal(util_mul_div64(UINT64_MAX, 1000000000ULL, 1000000000ULL), UINT64_MAX);
	assert_int_equal(util_mul_div64(3, 0x8000000000000000ULL, 2), 0xC000000000000000ULL);
	assert_int_equal(util_mul_div64(0x8000000000000000ULL, 2, 4), 0x4000000000000000ULL);
}

static void test_remainder_product_overflow(void **state)
{
	UNUSED_PARAMETER(state);

	/* rem * mul overflows 64 bits. MSVC x64 computes the exact quotient;
	 * everywhere else rem * mul wraps first.
	 * Characterized, not endorsed. */
#if UTIL_MUL_DIV64_EXACT
	assert_int_equal(util_mul_div64(0x1FFFFFFFFULL, 0x100000000ULL, 0x200000000ULL), 0xFFFFFFFFULL);
	assert_int_equal(util_mul_div64(5, UINT64_MAX, UINT64_MAX), 5ULL);
	assert_int_equal(util_mul_div64(UINT64_MAX - 1, UINT64_MAX, UINT64_MAX), UINT64_MAX - 1);
#else
	assert_int_equal(util_mul_div64(0x1FFFFFFFFULL, 0x100000000ULL, 0x200000000ULL), 0x7FFFFFFFULL);
	assert_int_equal(util_mul_div64(5, UINT64_MAX, UINT64_MAX), 0ULL);
	assert_int_equal(util_mul_div64(UINT64_MAX - 1, UINT64_MAX, UINT64_MAX), 0ULL);
#endif
}

#if !UTIL_MUL_DIV64_EXACT
static void test_quotient_overflow_wraps(void **state)
{
	UNUSED_PARAMETER(state);

	/* The quotient does not fit in 64 bits; (num / div) * mul wraps.
	 * On MSVC x64, _udiv128 faults instead, so this is not tested there.
	 * Characterized, not endorsed. */
	assert_int_equal(util_mul_div64(0x8000000000000000ULL, 4, 1), 0ULL);
	assert_int_equal(util_mul_div64(UINT64_MAX, 2, 1), UINT64_MAX - 1);
}
#endif

int main(void)
{
	const struct CMUnitTest tests[] = {
		cmocka_unit_test(test_audio_frames_to_ns),
		cmocka_unit_test(test_ns_to_audio_frames),
		cmocka_unit_test(test_frame_interval),
		cmocka_unit_test(test_zero_operands),
		cmocka_unit_test(test_div_one),
		cmocka_unit_test(test_large_values_without_overflow),
		cmocka_unit_test(test_remainder_product_overflow),
#if !UTIL_MUL_DIV64_EXACT
		cmocka_unit_test(test_quotient_overflow_wraps),
#endif
	};

	return cmocka_run_group_tests(tests, NULL, NULL);
}
