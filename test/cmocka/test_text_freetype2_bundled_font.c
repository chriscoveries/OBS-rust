#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <cmocka.h>

#include <obs-module.h>

#include "find-font.h"
#include "text-freetype2-host.h"

/* The Text (FreeType 2) source defaults to JetBrainsMono Nerd Font on every
 * OS and bundles it (obs-rust/obs-studio#1012), so the default face must
 * resolve to the bundled files even where the font isn't installed, as in
 * the Tier 2 container. BUNDLED_FONT_DIR is the plugin's data/fonts.
 *
 * main() points fontconfig at ISOLATED_FONTCONFIG, which lists no system font
 * directories, so only the bundled fonts are visible. Otherwise a machine
 * with the face installed resolves it to the system copy (obs-rust/obs-studio#1076). */

static void assert_bundled(uint32_t flags, const char *file)
{
	FT_Long idx = -1;
	const char *path = get_font_path(ft2_platform_default_face(), 32, "", flags, &idx);
	assert_non_null(path);
	assert_int_equal(strncmp(path, BUNDLED_FONT_DIR, strlen(BUNDLED_FONT_DIR)), 0);
	assert_non_null(strstr(path, file));
	assert_int_equal(idx, 0);
}

static void default_face_is_jetbrains_mono_nerd_font(void **state)
{
	(void)state;
	assert_string_equal(ft2_platform_default_face(), "JetBrainsMono Nerd Font");
}

static void regular_resolves_to_the_bundled_file(void **state)
{
	(void)state;
	assert_bundled(0, "JetBrainsMonoNerdFont-Regular.ttf");
}

static void bold_and_italic_resolve_to_their_bundled_files(void **state)
{
	(void)state;
	assert_bundled(OBS_FONT_BOLD, "JetBrainsMonoNerdFont-Bold.ttf");
	assert_bundled(OBS_FONT_ITALIC, "JetBrainsMonoNerdFont-Italic.ttf");
	assert_bundled(OBS_FONT_BOLD | OBS_FONT_ITALIC, "JetBrainsMonoNerdFont-BoldItalic.ttf");
}

static int register_bundled_fonts(void **state)
{
	(void)state;
	load_bundled_fonts(BUNDLED_FONT_DIR);
	return 0;
}

int main(void)
{
	/* Before the first fontconfig call, which loads the configuration. */
	setenv("FONTCONFIG_FILE", ISOLATED_FONTCONFIG, 1);

	const struct CMUnitTest tests[] = {
		cmocka_unit_test(default_face_is_jetbrains_mono_nerd_font),
		cmocka_unit_test(regular_resolves_to_the_bundled_file),
		cmocka_unit_test(bold_and_italic_resolve_to_their_bundled_files),
	};
	return cmocka_run_group_tests(tests, register_bundled_fonts, NULL);
}
