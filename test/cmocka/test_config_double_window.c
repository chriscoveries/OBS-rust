#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <cmocka.h>

#include "util/config-file.h"

/* os_strtod() copies at most 63 bytes of the stored string into a local
 * buffer before converting, so bytes 64 and up are never seen, and leading
 * whitespace and the sign count against the window. The window is taken
 * before strtod() looks at anything, so it applies to hex floats too. */

static uint64_t double_bits(double value)
{
	uint64_t bits;
	memcpy(&bits, &value, sizeof(bits));
	return bits;
}

struct window_case {
	const char *name;
	size_t lead_spaces;
	const char *prefix;
	size_t zeros;
	const char *tail;
	double expected;
};

static void build_value(const struct window_case *c, char *out, size_t out_size)
{
	size_t n = 0;
	memset(out, ' ', c->lead_spaces);
	n += c->lead_spaces;
	size_t prefix_len = strlen(c->prefix);
	memcpy(out + n, c->prefix, prefix_len);
	n += prefix_len;
	memset(out + n, '0', c->zeros);
	n += c->zeros;
	size_t tail_len = strlen(c->tail);
	assert_true(n + tail_len < out_size);
	memcpy(out + n, c->tail, tail_len);
	n += tail_len;
	out[n] = '\0';
}

static void test_double_conversion_window_boundary(void **state)
{
	(void)state;

	static const struct window_case cases[] = {
		/* 63 bytes: the number is inside the window. */
		{"spaces_63_bytes", 62, "", 0, "1", 1.0},
		/* 64 bytes: the digit is byte 64 and is dropped -> no conversion. */
		{"spaces_64_bytes", 63, "", 0, "1", 0.0},
		{"spaces_65_bytes", 64, "", 0, "1", 0.0},
		/* 0.<60 zeros>1 is 63 bytes: the last digit is byte 63. */
		{"fraction_63_bytes", 0, "0.", 60, "1", 1e-61},
		/* 0.<61 zeros>1 is 64 bytes: the last digit is dropped -> 0.0. */
		{"fraction_64_bytes", 0, "0.", 61, "1", 0.0},
		/* 1<62 zeros> is 63 bytes: 1e62. */
		{"integer_63_bytes", 0, "1", 62, "", 1e62},
		/* 1<63 zeros> is 64 bytes: the last zero is dropped, still 1e62. */
		{"integer_64_bytes", 0, "1", 63, "", 1e62},
		/* 1.<59 zeros>e5 is 63 bytes: exponent is inside the window. */
		{"exponent_63_bytes", 0, "1.", 59, "e5", 1e5},
		/* 1.<60 zeros>e5 is 64 bytes: the exponent digit is dropped, so
		 * the dangling "e" is ignored and the value is 1.0. */
		{"exponent_64_bytes", 0, "1.", 60, "e5", 1.0},
		/* 0x<60 zeros>1 is 63 bytes: the hex digit is byte 63. */
		{"hex_63_bytes", 0, "0x", 60, "1", 1.0},
		/* 0x<61 zeros>1 is 64 bytes: the hex digit is dropped -> 0.0. */
		{"hex_64_bytes", 0, "0x", 61, "1", 0.0},
		/* <61 spaces>0x1 is 64 bytes: only "0x" is left -> 0.0. The window
		 * counts from the first stored byte, not from the "0x". */
		{"hex_spaces_64_bytes", 61, "0x1", 0, "", 0.0},
	};

	for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
		const struct window_case *c = &cases[i];
		char value[160];
		build_value(c, value, sizeof(value));

		config_t *config = NULL;
		assert_int_equal(config_open_string(&config, ""), CONFIG_SUCCESS);
		assert_non_null(config);
		config_set_string(config, "Numbers", "value", value);
		config_set_default_string(config, "Numbers", "default_value", value);

		uint64_t expected = double_bits(c->expected);
		uint64_t user = double_bits(config_get_double(config, "Numbers", "value"));
		uint64_t def = double_bits(config_get_default_double(config, "Numbers", "default_value"));
		config_close(config);

		print_message("case %s (%zu bytes)\n", c->name, strlen(value));
		assert_int_equal(user, expected);
		assert_int_equal(def, expected);
	}
}

int main(void)
{
	const struct CMUnitTest tests[] = {
		cmocka_unit_test(test_double_conversion_window_boundary),
	};
	return cmocka_run_group_tests(tests, NULL, NULL);
}
