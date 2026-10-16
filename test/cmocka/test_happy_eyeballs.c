#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <string.h>
#include <cmocka.h>

#include <util/c99defs.h>
#include <happy-eyeballs.h>

#ifdef _WIN32
#define close_socket closesocket
#else
#include <netinet/in.h>
#include <arpa/inet.h>
#define INVALID_SOCKET -1
#define close_socket close
#endif

/*
 * Characterization test for shared/happy-eyeballs (obs-rust/obs-studio#880).
 * It pins what the connector does over loopback, so the move of its platform
 * code behind per-platform files (#847) can be checked on every OS: it must
 * pass unchanged before and after.
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

/* A listening socket on the loopback address of `family`, port picked by the
 * OS. Returns INVALID_SOCKET when the family is not available. */
static SOCKET listen_loopback(int family, int *port)
{
	SOCKET s = socket(family, SOCK_STREAM, IPPROTO_TCP);
	if (s == INVALID_SOCKET)
		return INVALID_SOCKET;

	struct sockaddr_storage ss;
	socklen_t len;
	memset(&ss, 0, sizeof(ss));
	if (family == AF_INET) {
		struct sockaddr_in *in = (struct sockaddr_in *)&ss;
		in->sin_family = AF_INET;
		in->sin_addr.s_addr = htonl(INADDR_LOOPBACK);
		len = sizeof(*in);
	} else {
		struct sockaddr_in6 *in6 = (struct sockaddr_in6 *)&ss;
		in6->sin6_family = AF_INET6;
		in6->sin6_addr = in6addr_loopback;
		len = sizeof(*in6);
	}

	if (bind(s, (struct sockaddr *)&ss, len) != 0 || listen(s, 4) != 0) {
		close_socket(s);
		return INVALID_SOCKET;
	}

	len = sizeof(ss);
	getsockname(s, (struct sockaddr *)&ss, &len);
	*port = ntohs(family == AF_INET ? ((struct sockaddr_in *)&ss)->sin_port
					: ((struct sockaddr_in6 *)&ss)->sin6_port);
	return s;
}

static void connect_ok(const char *host, int family)
{
	int port = 0;
	SOCKET server = listen_loopback(family, &port);
	if (server == INVALID_SOCKET) {
		/* IPv6 loopback is missing on some CI hosts. */
		assert_int_equal(family, AF_INET6);
		skip();
	}

	struct happy_eyeballs_ctx *ctx = NULL;
	assert_int_equal(happy_eyeballs_create(&ctx), 0);
	assert_non_null(ctx);

	int res = happy_eyeballs_connect(ctx, host, port);
	if (res != 0 && family == AF_INET6) {
		/* Name lookup uses AI_ADDRCONFIG, so "::1" does not resolve on a
		 * host without a non-loopback IPv6 address (Docker, some CI). */
		close_socket(server);
		happy_eyeballs_destroy(ctx);
		skip();
	}
	assert_int_equal(res, 0);
	assert_int_equal(happy_eyeballs_timedwait_default(ctx), 0);
	assert_int_equal(happy_eyeballs_get_error_code(ctx), 0);

	SOCKET fd = happy_eyeballs_get_socket_fd(ctx);
	assert_true(fd != INVALID_SOCKET);

	struct sockaddr_storage remote;
	memset(&remote, 0, sizeof(remote));
	/* returns the winner's address length, not a status */
	int addr_len = happy_eyeballs_get_remote_addr(ctx, &remote);
	assert_int_equal(addr_len,
			 family == AF_INET ? (int)sizeof(struct sockaddr_in) : (int)sizeof(struct sockaddr_in6));
	assert_int_equal(remote.ss_family, family);

	/* the connection is real: the server can accept it */
	SOCKET accepted = accept(server, NULL, NULL);
	assert_true(accepted != INVALID_SOCKET);

	close_socket(accepted);
	close_socket(fd);
	close_socket(server);
	assert_int_equal(happy_eyeballs_destroy(ctx), 0);
}

static void test_connect_ipv4(void **state)
{
	UNUSED_PARAMETER(state);
	connect_ok("127.0.0.1", AF_INET);
}

static void test_connect_ipv6(void **state)
{
	UNUSED_PARAMETER(state);
	connect_ok("::1", AF_INET6);
}

static void test_connect_refused(void **state)
{
	UNUSED_PARAMETER(state);

	/* a port that was just free: bind, read it, close */
	int port = 0;
	SOCKET server = listen_loopback(AF_INET, &port);
	assert_true(server != INVALID_SOCKET);
	close_socket(server);

	struct happy_eyeballs_ctx *ctx = NULL;
	assert_int_equal(happy_eyeballs_create(&ctx), 0);
	happy_eyeballs_connect(ctx, "127.0.0.1", port);
	assert_int_not_equal(happy_eyeballs_timedwait_default(ctx), 0);
	assert_int_not_equal(happy_eyeballs_get_error_code(ctx), 0);
	assert_non_null(happy_eyeballs_get_error_message(ctx));
	assert_int_equal(happy_eyeballs_destroy(ctx), 0);
}

static void test_unresolvable_host(void **state)
{
	UNUSED_PARAMETER(state);

	struct happy_eyeballs_ctx *ctx = NULL;
	assert_int_equal(happy_eyeballs_create(&ctx), 0);
	/* RFC 6761: .invalid never resolves */
	assert_int_not_equal(happy_eyeballs_connect(ctx, "obs-rust-test.invalid", 80), 0);
	/* Not pinned: the error code. On a resolve failure the connector stores
	 * GetSockError() (errno / WSAGetLastError) instead of getaddrinfo's
	 * result, so it is platform-dependent and usually 0 on Linux (#903). */
	assert_int_equal(happy_eyeballs_destroy(ctx), 0);
}

static void test_invalid_arguments(void **state)
{
	UNUSED_PARAMETER(state);

	struct happy_eyeballs_ctx *ctx = NULL;
	assert_int_equal(happy_eyeballs_create(&ctx), 0);
	assert_int_not_equal(happy_eyeballs_connect(ctx, NULL, 80), 0);
	assert_int_not_equal(happy_eyeballs_connect(ctx, "127.0.0.1", 0), 0);
	assert_int_not_equal(happy_eyeballs_connect(NULL, "127.0.0.1", 80), 0);
	assert_int_equal(happy_eyeballs_destroy(ctx), 0);
}

int main(void)
{
	const struct CMUnitTest tests[] = {
		cmocka_unit_test(test_connect_ipv4),      cmocka_unit_test(test_connect_ipv6),
		cmocka_unit_test(test_connect_refused),   cmocka_unit_test(test_unresolvable_host),
		cmocka_unit_test(test_invalid_arguments),
	};

	return cmocka_run_group_tests(tests, setup, teardown);
}
