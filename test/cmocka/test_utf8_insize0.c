#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <cmocka.h>

#include <wchar.h>

#include <util/utf8.h>

/* insize == 0 means "up to the first NUL". A forwarded-in-C build of utf8.c
 * at -O2 with the (unsigned char *)-1 sentinel used to fold the length check
 * and report 0 for any 3-byte sequence, so a size query for CJK answered
 * "nothing" and the round trip lost the string. */

static void test_insize0_three_byte_size_query(void **state)
{
	UNUSED_PARAMETER(state);

	/* U+4A13, and the first two kana of 日本. */
	assert_int_equal(utf8_to_wchar("\xe4\xa8\x93", 0, NULL, 0, 0), 1);
	assert_int_equal(utf8_to_wchar("\xe6\x97\xa5\xe6\x9c\xac", 0, NULL, 0, 0), 2);
}

static void test_insize0_three_byte_round_trip(void **state)
{
	UNUSED_PARAMETER(state);

	wchar_t wide[8] = {0};
	char narrow[16] = {0};

	assert_int_equal(utf8_to_wchar("\xe6\x97\xa5\xe6\x9c\xac", 0, wide, 8, 0), 2);
	assert_int_equal(wide[0], 0x65e5);
	assert_int_equal(wide[1], 0x672c);

	/* and back, so a loss in either direction fails */
	assert_int_equal(wchar_to_utf8(wide, 0, NULL, 0, 0), 6);
	assert_int_equal(wchar_to_utf8(wide, 0, narrow, 16, 0), 6);
	assert_memory_equal(narrow, "\xe6\x97\xa5\xe6\x9c\xac", 6);
}

static void test_insize0_shorter_inputs_unchanged(void **state)
{
	UNUSED_PARAMETER(state);

	wchar_t wide[8] = {0};

	/* 1 and 2 byte sequences keep their sizes */
	assert_int_equal(utf8_to_wchar("hello", 0, NULL, 0, 0), 5);
	assert_int_equal(utf8_to_wchar("\xc3\xa9", 0, NULL, 0, 0), 1);
	assert_int_equal(utf8_to_wchar("\xc3\xa9", 0, wide, 8, 0), 1);
	assert_int_equal(wide[0], 0x00e9);

	/* an embedded NUL still ends the scan */
	assert_int_equal(utf8_to_wchar("a\0b", 0, NULL, 0, 0), 1);

	/* an explicit insize still disagrees with the scan when it runs past it */
	assert_int_equal(utf8_to_wchar("\xe6\x97\xa5", 3, NULL, 0, 0), 1);
}

int main(void)
{
	const struct CMUnitTest tests[] = {
		cmocka_unit_test(test_insize0_three_byte_size_query),
		cmocka_unit_test(test_insize0_three_byte_round_trip),
		cmocka_unit_test(test_insize0_shorter_inputs_unchanged),
	};

	return cmocka_run_group_tests(tests, NULL, NULL);
}
