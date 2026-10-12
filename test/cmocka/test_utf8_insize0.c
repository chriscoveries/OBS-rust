#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <cmocka.h>

#include <wchar.h>
#include <string.h>

#include <util/c99defs.h>
#include <util/utf8.h>

/* insize == 0 means "up to the first NUL". A build of utf8.c at -O2 with the
 * (unsigned char *)-1 sentinel used to compute a negative extent for the
 * multi-byte checks and take the error path, which made the function answer 0
 * for 3- and 4-byte UTF-8. These assertions hold the -O0 behaviour in every
 * configuration. */

static void test_insize0_multibyte_size_query(void **state)
{
	UNUSED_PARAMETER(state);

	/* 1- and 2-byte are unaffected, 3- and 4-byte must survive. */
	assert_int_equal(utf8_to_wchar("hello", 0, NULL, 0, 0), 5);
	assert_int_equal(utf8_to_wchar("\xc3\xa9", 0, NULL, 0, 0), 1);

	assert_int_equal(utf8_to_wchar("\xe4\xa8\x93", 0, NULL, 0, 0), 1);
	assert_int_equal(utf8_to_wchar("\xe6\x97\xa5\xe6\x9c\xac", 0, NULL, 0, 0), 2);
	assert_int_equal(utf8_to_wchar("\xf0\x9f\x98\x80", 0, NULL, 0, 0), 1);
}

static void test_insize0_multibyte_round_trip(void **state)
{
	UNUSED_PARAMETER(state);

	wchar_t wide[8];
	char narrow[16];

	/* U+4A13, 3-byte */
	memset(wide, 0, sizeof(wide));
	assert_int_equal(utf8_to_wchar("\xe4\xa8\x93", 0, wide, 8, 0), 1);
	assert_int_equal(wide[0], 0x4a13);

	/* the first two kana of 日本, 3-byte each */
	memset(wide, 0, sizeof(wide));
	assert_int_equal(utf8_to_wchar("\xe6\x97\xa5\xe6\x9c\xac", 0, wide, 8, 0), 2);
	assert_int_equal(wide[0], 0x65e5);
	assert_int_equal(wide[1], 0x672c);

	/* and back, so a loss in either direction fails */
	memset(narrow, 0, sizeof(narrow));
	assert_int_equal(wchar_to_utf8(wide, 0, NULL, 0, 0), 6);
	assert_int_equal(wchar_to_utf8(wide, 0, narrow, 16, 0), 6);
	assert_memory_equal(narrow, "\xe6\x97\xa5\xe6\x9c\xac", 6);

	/* U+1F600, 4-byte */
	memset(wide, 0, sizeof(wide));
	assert_int_equal(utf8_to_wchar("\xf0\x9f\x98\x80", 0, wide, 8, 0), 1);
	assert_int_equal(wide[0], 0x1f600);

	memset(narrow, 0, sizeof(narrow));
	assert_int_equal(wchar_to_utf8(wide, 0, NULL, 0, 0), 4);
	assert_int_equal(wchar_to_utf8(wide, 0, narrow, 16, 0), 4);
	assert_memory_equal(narrow, "\xf0\x9f\x98\x80", 4);
}

static void test_insize0_other_inputs_unchanged(void **state)
{
	UNUSED_PARAMETER(state);

	wchar_t wide[8] = {0};
	char narrow[16] = {0};

	/* 2-byte converts and round trips */
	assert_int_equal(utf8_to_wchar("\xc3\xa9", 0, wide, 8, 0), 1);
	assert_int_equal(wide[0], 0x00e9);
	assert_int_equal(wchar_to_utf8(wide, 0, narrow, 16, 0), 2);
	assert_memory_equal(narrow, "\xc3\xa9", 2);

	/* an embedded NUL still ends the scan */
	assert_int_equal(utf8_to_wchar("a\0b", 0, NULL, 0, 0), 1);

	/* an explicit insize still bounds the conversion */
	assert_int_equal(utf8_to_wchar("\xe6\x97\xa5", 3, NULL, 0, 0), 1);
	assert_int_equal(utf8_to_wchar("\xe6\x97\xa5", 2, NULL, 0, 0), 0);

	/* a NULL argument is still rejected */
	assert_int_equal(utf8_to_wchar(NULL, 0, NULL, 0, 0), 0);
}

int main(void)
{
	const struct CMUnitTest tests[] = {
		cmocka_unit_test(test_insize0_multibyte_size_query),
		cmocka_unit_test(test_insize0_multibyte_round_trip),
		cmocka_unit_test(test_insize0_other_inputs_unchanged),
	};

	return cmocka_run_group_tests(tests, NULL, NULL);
}
