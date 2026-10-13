#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <stdlib.h>
#include <string.h>
#include <cmocka.h>

#include <util/dstr.h>

/* Regression tests for dstr.c bugs fixed after the Phase 3
 * characterization tests (test_dstr.c pins the behavior that was kept). */

/* obs-rust/obs-studio#46: a mid-string dstr_insert_ch moved one byte too
 * many and wrote array[capacity] when capacity was exactly new_len + 1.
 * The dstr points at a stack buffer with that capacity and a canary right
 * past it, so the overrun is caught without a sanitizer. */
static void insert_ch_stays_in_bounds(void **state)
{
	UNUSED_PARAMETER(state);

	char buf[8];
	memset(buf, 'Z', sizeof(buf));
	memcpy(buf, "ac", 3);
	buf[3] = 'Y'; /* inside capacity, past the NUL */

	struct dstr d = {buf, 2, 4};
	dstr_insert_ch(&d, 1, 'b');

	/* capacity 4 already fits "abc": no reallocation */
	assert_ptr_equal(d.array, buf);
	assert_string_equal(d.array, "abc");
	assert_int_equal(d.len, 3);
	assert_int_equal(d.capacity, 4);
	/* the byte past the buffer is untouched */
	assert_int_equal(buf[4], 'Z');
}

/* obs-rust/obs-studio#47: an empty find string made dstr_replace loop
 * forever (strstr(temp, "") never advances). An empty or NULL find is now
 * a no-op. */
static void replace_empty_find_is_noop(void **state)
{
	UNUSED_PARAMETER(state);

	struct dstr d;
	dstr_init_copy(&d, "abc");

	dstr_replace(&d, "", "x");
	dstr_replace(&d, "", "");
	dstr_replace(&d, "", NULL);
	dstr_replace(&d, NULL, "x");

	assert_string_equal(d.array, "abc");
	assert_int_equal(d.len, 3);

	dstr_free(&d);
}

/* A bounded compare accepts a non-terminated buffer when it has n readable
 * bytes. This also catches an FFI shim that runs strlen before the compare. */
static void astrcmp_n_reads_only_n_bytes(void **state)
{
	UNUSED_PARAMETER(state);

	char *bom = malloc(3);
	assert_non_null(bom);
	memcpy(bom, "\xEF\xBB\xBF", 3);

	assert_int_equal(astrcmp_n(bom, "\xEF\xBB\xBF", 3), 0);

	free(bom);
}

int main(void)
{
	const struct CMUnitTest tests[] = {
		cmocka_unit_test(insert_ch_stays_in_bounds),
		cmocka_unit_test(replace_empty_find_is_noop),
		cmocka_unit_test(astrcmp_n_reads_only_n_bytes),
	};

	return cmocka_run_group_tests(tests, NULL, NULL);
}
