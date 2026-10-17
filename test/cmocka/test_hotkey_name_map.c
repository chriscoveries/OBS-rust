#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <stdio.h>
#include <string.h>
#include <cmocka.h>

#include <obs.h>

/*
 * Characterization tests for libobs/obs-hotkey-name-map.c
 * (obs-rust/obs-studio#833). They pin CURRENT behavior. Without a core
 * (obs == NULL) obs_key_from_name scans the key list; after obs_startup it
 * looks names up in a map built once. Both paths must give the same answers.
 */

/* The key list, in enum order, straight from the header the C uses. */
static const char *key_names[] = {
#define OBS_HOTKEY(x) #x,
#include <obs-hotkeys.h>
#undef OBS_HOTKEY
};

#define NUM_KEYS (sizeof(key_names) / sizeof(key_names[0]))

static void check_names(void)
{
	assert_int_equal(NUM_KEYS, OBS_KEY_LAST_VALUE);

	/* every key round-trips code -> name -> code */
	for (size_t i = 0; i < NUM_KEYS; i++) {
		obs_key_t key = (obs_key_t)i;
		const char *name = obs_key_to_name(key);
		assert_string_equal(name, key_names[i]);
		assert_int_equal(obs_key_from_name(name), key);
	}

	assert_int_equal(obs_key_from_name("OBS_KEY_A"), OBS_KEY_A);
	assert_int_equal(obs_key_from_name("OBS_KEY_NONE"), OBS_KEY_NONE);
	assert_string_equal(obs_key_to_name(OBS_KEY_NONE), "OBS_KEY_NONE");

	/* unknown names are OBS_KEY_NONE; matching is exact and case sensitive */
	assert_int_equal(obs_key_from_name(""), OBS_KEY_NONE);
	assert_int_equal(obs_key_from_name("OBS_KEY_"), OBS_KEY_NONE);
	assert_int_equal(obs_key_from_name("obs_key_a"), OBS_KEY_NONE);
	assert_int_equal(obs_key_from_name("OBS_KEY_A "), OBS_KEY_NONE);
	assert_int_equal(obs_key_from_name("OBS_KEY_LAST_VALUE"), OBS_KEY_NONE);

	/* codes at and past the end log an error and give "" */
	assert_string_equal(obs_key_to_name(OBS_KEY_LAST_VALUE), "");
	assert_string_equal(obs_key_to_name((obs_key_t)(OBS_KEY_LAST_VALUE + 1)), "");
	assert_string_equal(obs_key_to_name((obs_key_t)100000), "");
}

static void test_without_core(void **state)
{
	UNUSED_PARAMETER(state);

	assert_false(obs_initialized());
	check_names();
}

static void test_with_core(void **state)
{
	UNUSED_PARAMETER(state);

	if (!obs_startup("en-US", NULL, NULL)) {
		/* obs_startup() needs a display on Linux; skip without one. */
		fprintf(stderr, "skipped: obs_startup failed (no display?)\n");
		skip();
	}

	check_names();
	/* the map is built once; a second pass reads the same map */
	check_names();

	obs_shutdown();
}

int main(void)
{
	const struct CMUnitTest tests[] = {
		cmocka_unit_test(test_without_core),
		cmocka_unit_test(test_with_core),
	};

	return cmocka_run_group_tests(tests, NULL, NULL);
}
