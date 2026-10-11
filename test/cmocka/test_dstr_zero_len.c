/* dstr_left() with pos == 0 passed a NULL destination to memcpy (UB, C11
 * 7.24.1p2), hit by os_generate_formatted_filename() whenever the filename
 * format starts with a token (obs-rust/obs-studio#174). This test compiles
 * libobs/util/dstr.c with UBSan aborting on the first report, so the UB
 * fails the test instead of being recovered from. */
#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <cmocka.h>

#include <util/dstr.h>

static void left_zero_of_text(void **state)
{
	(void)state;
	struct dstr s = {0}, d = {0};
	dstr_copy(&s, "%CCYY-%MM");
	dstr_left(&d, &s, 0);
	assert_int_equal(d.len, 0);
	dstr_free(&s);
	dstr_free(&d);
}

static void left_zero_of_empty(void **state)
{
	(void)state;
	struct dstr s = {0}, d = {0};
	dstr_left(&d, &s, 0);
	assert_int_equal(d.len, 0);
	dstr_free(&d);
}

static void left_keeps_prefix(void **state)
{
	(void)state;
	struct dstr s = {0}, d = {0};
	dstr_copy(&s, "abcdef");
	dstr_left(&d, &s, 3);
	assert_string_equal(d.array, "abc");
	dstr_free(&s);
	dstr_free(&d);
}

static void right_of_empty(void **state)
{
	(void)state;
	struct dstr s = {0}, d = {0};
	dstr_right(&d, &s, 0);
	assert_int_equal(d.len, 0);
	dstr_free(&d);
}

int main(void)
{
	const struct CMUnitTest tests[] = {
		cmocka_unit_test(left_zero_of_text),
		cmocka_unit_test(left_zero_of_empty),
		cmocka_unit_test(left_keeps_prefix),
		cmocka_unit_test(right_of_empty),
	};
	return cmocka_run_group_tests(tests, NULL, NULL);
}
