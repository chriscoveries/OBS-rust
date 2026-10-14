#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <cmocka.h>

#include "saved-transition.h"

/* The built-in transitions (Cut, Fade, ...) are named in the UI language:
 * "Cut" in English is "Coupure" in French. A scene collection saved with Cut
 * selected stores "Cut"; after switching OBS to French it finds no transition
 * named "Cut" and falls back to Fade ("Fondu"), and saves that, so the choice
 * is lost even after switching back.
 *
 * The model below stands in for OBSBasic's transition list after a load in
 * French: the built-in Fade and Cut under their French names and a
 * user-created "Fade to Color" transition named "Black". */

struct transition {
	const char *name;
	const char *id;
	bool builtin;
};

static struct transition fade = {"Fondu", "fade_transition", true};
static struct transition cut = {"Coupure", "cut_transition", true};
static struct transition black = {"Black", "fade_to_color_transition", false};
static struct transition *list[] = {&fade, &cut, &black};

static void *find_by_name(void *param, const char *name)
{
	(void)param;
	for (size_t i = 0; i < sizeof(list) / sizeof(list[0]); i++)
		if (strcmp(list[i]->name, name) == 0)
			return list[i];
	return NULL;
}

static void *find_builtin_by_id(void *param, const char *id)
{
	(void)param;
	for (size_t i = 0; i < sizeof(list) / sizeof(list[0]); i++)
		if (list[i]->builtin && strcmp(list[i]->id, id) == 0)
			return list[i];
	return NULL;
}

static const struct saved_transition_ops ops = {
	.param = NULL,
	.find_by_name = find_by_name,
	.find_builtin_by_id = find_builtin_by_id,
	.fallback = &fade,
};

/* Saved in English with Cut selected, loaded in French. */
static void builtin_saved_in_another_language_is_kept(void **state)
{
	(void)state;
	assert_ptr_equal(saved_transition_find(&ops, "Cut", "cut_transition"), &cut);
	assert_ptr_equal(saved_transition_find(&ops, "Fade", "fade_transition"), &fade);
}

/* Same language: found by name. */
static void transition_found_by_name(void **state)
{
	(void)state;
	assert_ptr_equal(saved_transition_find(&ops, "Coupure", "cut_transition"), &cut);
	assert_ptr_equal(saved_transition_find(&ops, "Black", "fade_to_color_transition"), &black);
}

/* A user-created transition that is gone is not replaced by another one of
 * the same type: those can exist several times with different settings. */
static void missing_configurable_transition_falls_back(void **state)
{
	(void)state;
	assert_ptr_equal(saved_transition_find(&ops, "White", "fade_to_color_transition"), &fade);
}

/* Collections saved before the id was stored, or with nothing saved. */
static void without_id_falls_back(void **state)
{
	(void)state;
	assert_ptr_equal(saved_transition_find(&ops, "Cut", ""), &fade);
	assert_ptr_equal(saved_transition_find(&ops, "Cut", NULL), &fade);
	assert_ptr_equal(saved_transition_find(&ops, "", ""), &fade);
	assert_ptr_equal(saved_transition_find(&ops, NULL, NULL), &fade);
}

int main(void)
{
	const struct CMUnitTest tests[] = {
		cmocka_unit_test(builtin_saved_in_another_language_is_kept),
		cmocka_unit_test(transition_found_by_name),
		cmocka_unit_test(missing_configurable_transition_falls_back),
		cmocka_unit_test(without_id_falls_back),
	};

	return cmocka_run_group_tests(tests, NULL, NULL);
}
