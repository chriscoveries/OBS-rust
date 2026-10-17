#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <cmocka.h>

#include <obs-core-modules.h>

static void required_core_modules_are_listed(void **state)
{
	(void)state;

	assert_true(obs_core_modules_count >= 2);
	assert_true(obs_in_core_module_list("obs-outputs"));
	assert_true(obs_in_core_module_list("obs-transitions"));
}

int main(void)
{
	const struct CMUnitTest tests[] = {
		cmocka_unit_test(required_core_modules_are_listed),
	};
	return cmocka_run_group_tests(tests, NULL, NULL);
}
