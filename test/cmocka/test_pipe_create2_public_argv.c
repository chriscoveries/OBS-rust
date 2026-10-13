#include <stddef.h>
#include <stdint.h>
#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <cmocka.h>

#include "util/pipe.h"

static struct os_process_args *printf_args(void)
{
	struct os_process_args *args = os_process_args_create("/usr/bin/printf");
	assert_non_null(args);
	os_process_args_add_arg(args, "%s:%s");
	os_process_args_add_arg(args, "A");
	os_process_args_add_arg(args, "B");

	assert_int_equal(os_process_args_get_argc(args), 4);
	char **argv = os_process_args_get_argv(args);
	assert_non_null(argv);
	assert_null(argv[4]);
	return args;
}

static void swap_argv(char **argv, size_t a, size_t b)
{
	char *tmp = argv[a];
	argv[a] = argv[b];
	argv[b] = tmp;
}

static void read_all(os_process_pipe_t *pp, char *output, size_t size)
{
	size_t len = 0;
	while (len < size - 1) {
		size_t n = os_process_pipe_read(pp, (uint8_t *)output + len, size - 1 - len);
		if (n == 0)
			break;
		len += n;
	}
	output[len] = '\0';
}

static void test_create2_uses_public_argv_order(void **state)
{
	(void)state;
	struct os_process_args *args = printf_args();
	char **argv = os_process_args_get_argv(args);
	swap_argv(argv, 2, 3);

	os_process_pipe_t *pp = os_process_pipe_create2(args, "r");
	assert_non_null(pp);
	char output[32];
	read_all(pp, output, sizeof(output));
	assert_int_equal(os_process_pipe_destroy(pp), 0);
	os_process_args_destroy(args);

	assert_string_equal(output, "B:A");
}

/* C hands the public array to posix_spawn as-is, so a NULL in the middle
 * ends the child's argument list there. */
static void test_create2_stops_at_public_argv_null(void **state)
{
	(void)state;
	struct os_process_args *args = printf_args();
	char **argv = os_process_args_get_argv(args);
	char *saved = argv[2];
	argv[2] = NULL;

	os_process_pipe_t *pp = os_process_pipe_create2(args, "r");
	argv[2] = saved;
	assert_non_null(pp);
	char output[32];
	read_all(pp, output, sizeof(output));
	int status = os_process_pipe_destroy(pp);
	os_process_args_destroy(args);

	assert_int_equal(status, 0);
	assert_string_equal(output, ":");
}

/* C takes the executable from the public argv[0], so after this swap it
 * tries to run "%s:%s", which does not exist. */
static void test_create2_runs_public_argv0(void **state)
{
	(void)state;
	struct os_process_args *args = printf_args();
	char **argv = os_process_args_get_argv(args);
	swap_argv(argv, 0, 1);

	os_process_pipe_t *pp = os_process_pipe_create2(args, "r");
	if (pp)
		(void)os_process_pipe_destroy(pp);
	os_process_args_destroy(args);

	assert_null(pp);
}

int main(void)
{
	const struct CMUnitTest tests[] = {
		cmocka_unit_test(test_create2_uses_public_argv_order),
		cmocka_unit_test(test_create2_stops_at_public_argv_null),
		cmocka_unit_test(test_create2_runs_public_argv0),
	};
	return cmocka_run_group_tests(tests, NULL, NULL);
}
