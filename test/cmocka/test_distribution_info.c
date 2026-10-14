/* Startup distribution diagnostics, obs-rust/obs-studio#633. */
#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <cmocka.h>
#include "obs-nix-os-release.h"

static void check_distribution(const char *input, const char *name, const char *expected_version)
{
	FILE *fp = fmemopen((void *)input, strlen(input), "r");
	assert_non_null(fp);
	struct dstr distro, version;
	read_distribution_info(fp, &distro, &version);
	assert_string_equal(distro.array, name);
	assert_string_equal(version.array, expected_version);
	dstr_free(&distro);
	dstr_free(&version);
	fclose(fp);
}

static void arch_build_id(void **state)
{
	(void)state;
	check_distribution("NAME=\"Arch Linux\"\nBUILD_ID=rolling\n", "Arch Linux", "rolling");
}

static void rolling_without_build_id(void **state)
{
	(void)state;
	check_distribution("NAME='Arch Linux'\n", "Arch Linux", "rolling");
}

static void version_precedes_build_id(void **state)
{
	(void)state;
	check_distribution("NAME=Ubuntu\nVERSION_ID=\"26.04\"\nBUILD_ID=123\n", "Ubuntu", "26.04");
	check_distribution("BUILD_ID=123\nNAME=Ubuntu\nVERSION_ID=26.04", "Ubuntu", "26.04");
}

static void exact_keys_and_empty_values(void **state)
{
	(void)state;
	check_distribution("NAME_VARIANT=wrong\nNAME=Arch\nVERSION_ID=\"\"\nBUILD_ID=\"20261013\"\n", "Arch",
			   "20261013");
	check_distribution("# NAME=wrong\nNAME=\nVERSION_ID_SUFFIX=wrong\n", "Unknown", "Unknown");
}

int main(void)
{
	const struct CMUnitTest tests[] = {cmocka_unit_test(arch_build_id), cmocka_unit_test(rolling_without_build_id),
					   cmocka_unit_test(version_precedes_build_id),
					   cmocka_unit_test(exact_keys_and_empty_values)};
	return cmocka_run_group_tests(tests, NULL, NULL);
}
