#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <string.h>
#include <cmocka.h>

#include <obs-properties.h>

/*
 * obs-rust/obs-studio#831: built with AddressSanitizer (LeakSanitizer), so
 * a leak fails the test at exit.
 */

/* A file-save path's filter was only freed for OBS_PATH_FILE. */
static void file_save_path_filter_is_freed(void **state)
{
	UNUSED_PARAMETER(state);

	obs_properties_t *props = obs_properties_create();
	obs_property_t *p = obs_properties_add_path(props, "save", "Save", OBS_PATH_FILE_SAVE, "Video (*.mp4)", "/out");
	assert_non_null(p);
	assert_string_equal(obs_property_path_filter(p), "Video (*.mp4)");
	obs_properties_destroy(props);
}

/* Replacing a button URL leaked the previous one. */
static void replaced_button_url_is_freed(void **state)
{
	UNUSED_PARAMETER(state);

	obs_properties_t *props = obs_properties_create();
	obs_property_t *b = obs_properties_add_button2(props, "b", "B", NULL, NULL);
	assert_non_null(b);
	char first[] = "https://a.example";
	char second[] = "https://b.example";
	obs_property_button_set_url(b, first);
	obs_property_button_set_url(b, second);
	assert_string_equal(obs_property_button_url(b), "https://b.example");
	obs_properties_destroy(props);
}

int main(void)
{
	const struct CMUnitTest tests[] = {
		cmocka_unit_test(file_save_path_filter_is_freed),
		cmocka_unit_test(replaced_button_url_is_freed),
	};

	return cmocka_run_group_tests(tests, NULL, NULL);
}
