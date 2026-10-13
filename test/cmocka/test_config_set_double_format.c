#include <float.h>
#include <math.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <setjmp.h>

#include <cmocka.h>

#include "util/config-file.h"
#include "util/platform.h"

/* config_set_double() stores os_dtostr(value, buf, 64): "%.17g", then ".0"
 * appended when the text has neither '.' nor 'e'. That rule also applies to
 * the non-finite spellings ("inf" -> "inf.0", "nan" -> "nan.0"). os_dtostr
 * itself is the C reference for the byte-exact comparisons here. */

static double double_from_bits(uint64_t bits)
{
	double value;
	memcpy(&value, &bits, sizeof(value));
	return value;
}

static void expect_set_double(double value, const char *literal)
{
	char reference[64];
	assert_int_equal(os_dtostr(value, reference, sizeof(reference)) > 0, 1);

	config_t *config = NULL;
	assert_int_equal(config_open_string(&config, ""), CONFIG_SUCCESS);
	assert_non_null(config);
	config_set_double(config, "Numbers", "value", value);
	const char *stored = config_get_string(config, "Numbers", "value");
	assert_non_null(stored);

	assert_string_equal(stored, reference);
	if (literal)
		assert_string_equal(stored, literal);
	config_close(config);
}

static void test_set_double_nonfinite_spelling(void **state)
{
	(void)state;

	expect_set_double(INFINITY, "inf.0");
	expect_set_double(-INFINITY, "-inf.0");
	expect_set_double(NAN, "nan.0");
}

static void test_set_double_nan_variants(void **state)
{
	(void)state;

	/* Sign and payload spellings are libc-specific, so compare to C only. */
	expect_set_double(-NAN, NULL);
	expect_set_double(double_from_bits(UINT64_C(0x7ff8000000001234)), NULL);
	expect_set_double(double_from_bits(UINT64_C(0xfff8000000001234)), NULL);
	expect_set_double(double_from_bits(UINT64_C(0x7ff0000000000001)), NULL);
}

/* config_set_default_double() stores dstr_printf("%g") with no ".0" rule,
 * so non-finite defaults keep the bare C library spelling. */
static void expect_set_default_double(double value)
{
	char reference[64];
	assert_int_equal(snprintf(reference, sizeof(reference), "%g", value) > 0, 1);

	config_t *config = NULL;
	assert_int_equal(config_open_string(&config, ""), CONFIG_SUCCESS);
	assert_non_null(config);
	config_set_default_double(config, "Numbers", "value", value);
	const char *stored = config_get_default_string(config, "Numbers", "value");
	assert_non_null(stored);
	assert_string_equal(stored, reference);
	stored = config_get_string(config, "Numbers", "value");
	assert_non_null(stored);
	assert_string_equal(stored, reference);
	config_close(config);
}

static void test_set_default_double_nonfinite(void **state)
{
	(void)state;

	expect_set_default_double(INFINITY);
	expect_set_default_double(-INFINITY);
	expect_set_default_double(NAN);
	expect_set_default_double(-NAN);
	expect_set_default_double(double_from_bits(UINT64_C(0xfff8000000001234)));
	expect_set_default_double(double_from_bits(UINT64_C(0x7ff0000000000001)));
	expect_set_default_double(1.5);
}

static void test_set_double_nonfinite_round_trip(void **state)
{
	(void)state;

	config_t *config = NULL;
	assert_int_equal(config_open_string(&config, ""), CONFIG_SUCCESS);
	config_set_double(config, "Numbers", "pos", INFINITY);
	config_set_double(config, "Numbers", "neg", -INFINITY);
	config_set_double(config, "Numbers", "nan", NAN);
	assert_true(config_get_double(config, "Numbers", "pos") == INFINITY);
	assert_true(config_get_double(config, "Numbers", "neg") == -INFINITY);
	assert_true(isnan(config_get_double(config, "Numbers", "nan")));
	config_close(config);
}

static void test_set_double_finite_unchanged(void **state)
{
	(void)state;

	expect_set_double(0.0, "0.0");
	expect_set_double(-0.0, "-0.0");
	expect_set_double(1.0, "1.0");
	expect_set_double(-1.0, "-1.0");
	expect_set_double(1.5, "1.5");
	expect_set_double(100.0, "100.0");
	expect_set_double(0.1, "0.10000000000000001");
	expect_set_double(1e15, "1000000000000000.0");
	expect_set_double(1e16, "10000000000000000.0");
	/* Exponent forms already contain 'e': no ".0" and C's exponent quirks. */
	expect_set_double(1e17, NULL);
	expect_set_double(1e18, NULL);
	expect_set_double(1e100, NULL);
	expect_set_double(1.5e-5, NULL);
	expect_set_double(1e-100, NULL);
	expect_set_double(DBL_MAX, NULL);
	expect_set_double(-DBL_MAX, NULL);
	expect_set_double(DBL_MIN, NULL);
	expect_set_double(double_from_bits(1), NULL);
	expect_set_double(123456789012345.6, NULL);
}

int main(void)
{
	const struct CMUnitTest tests[] = {
		cmocka_unit_test(test_set_double_nonfinite_spelling),
		cmocka_unit_test(test_set_double_nan_variants),
		cmocka_unit_test(test_set_default_double_nonfinite),
		cmocka_unit_test(test_set_double_nonfinite_round_trip),
		cmocka_unit_test(test_set_double_finite_unchanged),
	};
	return cmocka_run_group_tests(tests, NULL, NULL);
}
