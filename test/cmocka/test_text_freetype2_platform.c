#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <cmocka.h>

#include <util/c99defs.h>
#include "text-freetype2-host.h"

/*
 * The FreeType text source's per-platform defaults (obs-rust/obs-studio#863)
 * on Linux and the BSDs. The default face is the bundled JetBrainsMono Nerd
 * Font on every OS (obs-rust/obs-studio#1012); the Windows (deprecated) and
 * macOS files are checked by their native builds.
 */

static void test_unix_defaults(void **state)
{
	UNUSED_PARAMETER(state);

	assert_string_equal(ft2_platform_default_face(), "JetBrainsMono Nerd Font");
	assert_int_equal(ft2_platform_source_flags(), 0);
}

int main(void)
{
	const struct CMUnitTest tests[] = {
		cmocka_unit_test(test_unix_defaults),
	};

	return cmocka_run_group_tests(tests, NULL, NULL);
}
