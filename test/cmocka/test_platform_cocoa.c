#include <setjmp.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <cmocka.h>

/*
 * Characterization of libobs/util/platform-cocoa.m. test_platform.c does
 * not call this file. Off Apple the binary is a no-op so the cmocka
 * aggregate still builds. The assertions below stay unchanged once the
 * Rust port swaps the file in.
 */

#ifdef __APPLE__
#include <CoreFoundation/CoreFoundation.h>
#include <math.h>

#include <util/apple/cfstring-utils.h>
#include <util/bmem.h>
#include <util/dstr.h>
#include <util/platform.h>

static void test_config_path_application_support(void **state)
{
	char buf[1024];
	char *owned;

	(void)state;

	assert_true(os_get_config_path(buf, sizeof(buf), NULL) > 0);
	assert_non_null(strstr(buf, "Application Support"));
	assert_int_not_equal(buf[strlen(buf) - 1], '/');

	assert_true(os_get_config_path(buf, sizeof(buf), "") > 0);
	assert_int_not_equal(buf[strlen(buf) - 1], '/');

	assert_true(os_get_config_path(buf, sizeof(buf), "obs-cocoa-test") > 0);
	assert_non_null(strstr(buf, "Application Support/obs-cocoa-test"));

	owned = os_get_config_path_ptr(NULL);
	assert_non_null(owned);
	assert_non_null(strstr(owned, "Application Support"));
	assert_int_equal(owned[strlen(owned) - 1], '/');
	bfree(owned);

	owned = os_get_config_path_ptr("");
	assert_non_null(owned);
	assert_int_equal(owned[strlen(owned) - 1], '/');
	bfree(owned);

	owned = os_get_config_path_ptr("obs-cocoa-test");
	assert_non_null(owned);
	assert_non_null(strstr(owned, "Application Support/obs-cocoa-test"));
	assert_int_not_equal(owned[strlen(owned) - 1], '/');
	bfree(owned);
}

static void test_program_data_path(void **state)
{
	char buf[1024];
	char *owned;

	(void)state;

	assert_true(os_get_program_data_path(buf, sizeof(buf), NULL) > 0);
	assert_non_null(strstr(buf, "Application Support"));
	assert_int_not_equal(buf[strlen(buf) - 1], '/');

	owned = os_get_program_data_path_ptr("obs-cocoa-test");
	assert_non_null(owned);
	assert_non_null(strstr(owned, "Application Support/obs-cocoa-test"));
	bfree(owned);

	owned = os_get_program_data_path_ptr(NULL);
	assert_non_null(owned);
	assert_int_equal(owned[strlen(owned) - 1], '/');
	bfree(owned);
}

static void test_executable_path_ptr(void **state)
{
	char *owned;

	(void)state;

	owned = os_get_executable_path_ptr(NULL);
	assert_non_null(owned);
	assert_int_equal(owned[strlen(owned) - 1], '/');
	bfree(owned);

	owned = os_get_executable_path_ptr("");
	assert_non_null(owned);
	assert_int_equal(owned[strlen(owned) - 1], '/');
	bfree(owned);

	owned = os_get_executable_path_ptr("obs-cocoa-name");
	assert_non_null(owned);
	assert_non_null(strstr(owned, "/obs-cocoa-name"));
	bfree(owned);
}

static void test_gettime_ns_monotonic(void **state)
{
	uint64_t first;
	uint64_t second;

	(void)state;

	first = os_gettime_ns();
	second = os_gettime_ns();
	assert_true(first > 0);
	assert_true(second >= first);
}

static void test_core_counts_stable(void **state)
{
	int physical;
	int logical;

	(void)state;

	physical = os_get_physical_cores();
	logical = os_get_logical_cores();
	assert_int_equal(physical, os_get_physical_cores());
	assert_int_equal(logical, os_get_logical_cores());
	assert_true(physical > 0);
	assert_true(logical >= physical);
}

static void test_sys_memory(void **state)
{
	os_proc_memory_usage_t usage;

	(void)state;

	assert_true(os_get_sys_total_size() > 0);
	assert_true(os_get_sys_free_size() > 0);
	assert_true(os_get_proc_resident_size() > 0);
	assert_true(os_get_proc_virtual_size() > 0);
	assert_true(os_get_proc_memory_usage(&usage));
	assert_true(usage.resident_size > 0);
	assert_true(usage.virtual_size > 0);
}

static void test_emulation_status(void **state)
{
	(void)state;

#ifdef __aarch64__
	assert_false(os_get_emulation_status());
#else
	(void)os_get_emulation_status();
#endif
}

static void test_cpu_usage_null_query(void **state)
{
	os_cpu_usage_info_t *info;
	double sample;

	(void)state;

	info = os_cpu_usage_info_start();
	assert_non_null(info);
	sample = os_cpu_usage_info_query(info);
	assert_false(isnan(sample));
	assert_true(os_cpu_usage_info_query(NULL) == 0.0);
	os_cpu_usage_info_destroy(NULL);
	os_cpu_usage_info_destroy(info);
}

static void test_high_performance_null_end(void **state)
{
	os_performance_token_t *token;

	(void)state;

	token = os_request_high_performance("obs-cocoa-test");
	assert_non_null((void *)token);
	os_end_high_performance(token);
	os_end_high_performance(NULL);
}

static void test_inhibit_null(void **state)
{
	os_inhibit_t *info;

	(void)state;

	/* os_inhibit_sleep_create's condition is inverted: a null reason is
	 * passed to CFStringCreateWithCString, and any other reason becomes
	 * an empty string. The CFString sits in a private struct, so this
	 * test does not read it. Characterized, not endorsed. */
	info = os_inhibit_sleep_create("ignored-on-purpose");
	assert_non_null(info);
	assert_false(os_inhibit_sleep_set_active(NULL, true));
	assert_false(os_inhibit_sleep_set_active(info, false));
	os_inhibit_sleep_destroy(NULL);
	os_inhibit_sleep_destroy(info);
}

static void test_free_space_null(void **state)
{
	(void)state;

	assert_true(os_get_free_space(NULL) == 0);
	assert_true(os_get_free_disk_space(NULL) == 0);
	assert_true(os_get_free_space("/") > 0);
}

static void test_cfstr_null_and_roundtrip(void **state)
{
	struct dstr text;
	CFStringRef cf;
	char *owned;
	UniChar accent[] = {0x00E9};

	(void)state;

	assert_null(cfstr_copy_cstr(NULL, kCFStringEncodingUTF8));
	dstr_init(&text);
	assert_false(cfstr_copy_dstr(NULL, kCFStringEncodingUTF8, &text));

	cf = CFStringCreateWithCString(NULL, "obs-cocoa", kCFStringEncodingUTF8);
	assert_non_null(cf);
	owned = cfstr_copy_cstr(cf, kCFStringEncodingUTF8);
	assert_non_null(owned);
	assert_string_equal(owned, "obs-cocoa");
	bfree(owned);

	assert_true(cfstr_copy_dstr(cf, kCFStringEncodingUTF8, &text));
	assert_string_equal(text.array, "obs-cocoa");
	assert_true(text.len >= strlen("obs-cocoa"));
	assert_int_equal(text.array[text.len], '\0');
	dstr_free(&text);
	CFRelease(cf);

	/* Slow path sets len to the encoding maximum, not strlen. The quick
	 * path uses strlen. A non-ASCII character usually misses
	 * CFStringGetCStringPtr. Characterized, not endorsed. */
	cf = CFStringCreateWithCharacters(NULL, accent, 1);
	assert_non_null(cf);
	dstr_init(&text);
	assert_true(cfstr_copy_dstr(cf, kCFStringEncodingUTF8, &text));
	assert_string_equal(text.array, "\xC3\xA9");
	assert_true(text.len >= strlen(text.array));
	assert_int_equal(text.array[text.len], '\0');
	dstr_free(&text);
	owned = cfstr_copy_cstr(cf, kCFStringEncodingUTF8);
	assert_non_null(owned);
	assert_string_equal(owned, "\xC3\xA9");
	bfree(owned);
	CFRelease(cf);
}
#endif

#ifndef __APPLE__
static void test_platform_cocoa_is_apple_only(void **state)
{
	(void)state;
	assert_true(1);
}
#endif

int main(void)
{
#ifdef __APPLE__
	const struct CMUnitTest tests[] = {
		cmocka_unit_test(test_config_path_application_support),
		cmocka_unit_test(test_program_data_path),
		cmocka_unit_test(test_executable_path_ptr),
		cmocka_unit_test(test_gettime_ns_monotonic),
		cmocka_unit_test(test_core_counts_stable),
		cmocka_unit_test(test_sys_memory),
		cmocka_unit_test(test_emulation_status),
		cmocka_unit_test(test_cpu_usage_null_query),
		cmocka_unit_test(test_high_performance_null_end),
		cmocka_unit_test(test_inhibit_null),
		cmocka_unit_test(test_free_space_null),
		cmocka_unit_test(test_cfstr_null_and_roundtrip),
	};
#else
	const struct CMUnitTest tests[] = {
		cmocka_unit_test(test_platform_cocoa_is_apple_only),
	};
#endif

	return cmocka_run_group_tests(tests, NULL, NULL);
}
