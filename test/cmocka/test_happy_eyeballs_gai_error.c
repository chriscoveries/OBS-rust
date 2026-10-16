#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <string.h>
#include <cmocka.h>

#include <util/c99defs.h>
#include <happy-eyeballs.h>

/*
 * obs-rust/obs-studio#903: a failed name lookup must report the lookup's own
 * error. The connector stored GetSockError() (errno / WSAGetLastError())
 * instead of getaddrinfo's result, so on Linux the error code was 0 and the
 * message "Success".
 */

static int setup(void **state)
{
	UNUSED_PARAMETER(state);
#ifdef _WIN32
	WSADATA wsa;
	return WSAStartup(MAKEWORD(2, 2), &wsa);
#else
	return 0;
#endif
}

static int teardown(void **state)
{
	UNUSED_PARAMETER(state);
#ifdef _WIN32
	WSACleanup();
#endif
	return 0;
}

static void test_unresolvable_host_reports_lookup_error(void **state)
{
	UNUSED_PARAMETER(state);

	struct happy_eyeballs_ctx *ctx = NULL;
	assert_int_equal(happy_eyeballs_create(&ctx), 0);

	/* RFC 6761: .invalid never resolves */
	assert_int_not_equal(happy_eyeballs_connect(ctx, "obs-rust-test.invalid", 80), 0);

	assert_int_not_equal(happy_eyeballs_get_error_code(ctx), 0);
	const char *msg = happy_eyeballs_get_error_message(ctx);
	assert_non_null(msg);
	assert_true(*msg != '\0');
	assert_string_not_equal(msg, "Success");

	assert_int_equal(happy_eyeballs_destroy(ctx), 0);
}

int main(void)
{
	const struct CMUnitTest tests[] = {
		cmocka_unit_test(test_unresolvable_host_reports_lookup_error),
	};

	return cmocka_run_group_tests(tests, setup, teardown);
}
