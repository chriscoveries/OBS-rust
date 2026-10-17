#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <string.h>
#include <cmocka.h>

/* obs_properties_add_button is deprecated but still exported; it is pinned too. */
#if defined(__clang__) || defined(__GNUC__)
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#endif
#ifdef _MSC_VER
#pragma warning(disable : 4996)
#endif

#include <obs-properties.h>
#include <obs-data.h>
#include <util/bmem.h>
#include <util/darray.h>

/*
 * Characterization tests for libobs/obs-properties.c (obs-rust/obs-studio#828).
 * They pin CURRENT behavior, including odd behavior. Cases marked
 * "characterized, not endorsed" are quirks of the C implementation that a
 * port must reproduce.
 *
 * Two leaks are left out on purpose, so the test stays clean under the
 * sanitizers: a file-save path's filter is never freed, and
 * obs_property_button_set_url does not free a previous URL.
 */

/* ------------------------------------------------------------------------- */
/* Callback recorder */

#define MAX_CALLS 16

struct call {
	char name[32];
	obs_properties_t *props;
	void *priv;
};

static struct call calls[MAX_CALLS];
static size_t num_calls;

static void reset_calls(void)
{
	memset(calls, 0, sizeof(calls));
	num_calls = 0;
}

static void record(obs_properties_t *props, obs_property_t *p, void *priv)
{
	assert_true(num_calls < MAX_CALLS);
	strncpy(calls[num_calls].name, obs_property_name(p), sizeof(calls[num_calls].name) - 1);
	calls[num_calls].props = props;
	calls[num_calls].priv = priv;
	num_calls++;
}

static bool modified_cb(obs_properties_t *props, obs_property_t *p, obs_data_t *settings)
{
	UNUSED_PARAMETER(settings);
	record(props, p, NULL);
	return true;
}

static bool modified2_cb(void *priv, obs_properties_t *props, obs_property_t *p, obs_data_t *settings)
{
	UNUSED_PARAMETER(settings);
	record(props, p, priv);
	return false;
}

static bool clicked_cb(obs_properties_t *props, obs_property_t *p, void *data)
{
	record(props, p, data);
	return true;
}

static int destroyed;

static void destroy_param(void *param)
{
	UNUSED_PARAMETER(param);
	destroyed++;
}

/* ------------------------------------------------------------------------- */

static void test_create_param_flags(void **state)
{
	UNUSED_PARAMETER(state);

	int a = 1, b = 2;
	destroyed = 0;

	obs_properties_t *props = obs_properties_create_param(&a, destroy_param);
	assert_ptr_equal(obs_properties_get_param(props), &a);
	assert_int_equal(obs_properties_get_flags(props), 0);
	assert_null(obs_properties_first(props));
	assert_null(obs_properties_get_parent(props));

	obs_properties_set_flags(props, OBS_PROPERTIES_DEFER_UPDATE);
	assert_int_equal(obs_properties_get_flags(props), OBS_PROPERTIES_DEFER_UPDATE);

	/* replacing the param destroys the old one */
	obs_properties_set_param(props, &b, destroy_param);
	assert_int_equal(destroyed, 1);
	assert_ptr_equal(obs_properties_get_param(props), &b);

	obs_properties_destroy(props);
	assert_int_equal(destroyed, 2);

	/* NULL handles */
	assert_null(obs_properties_get_param(NULL));
	assert_int_equal(obs_properties_get_flags(NULL), 0);
	assert_null(obs_properties_first(NULL));
	assert_null(obs_properties_get(NULL, "x"));
	obs_properties_set_flags(NULL, 1);
	obs_properties_destroy(NULL);
}

static void test_common_fields(void **state)
{
	UNUSED_PARAMETER(state);

	obs_properties_t *props = obs_properties_create();
	obs_property_t *p = obs_properties_add_bool(props, "b", "Bool");

	assert_string_equal(obs_property_name(p), "b");
	assert_string_equal(obs_property_description(p), "Bool");
	assert_null(obs_property_long_description(p));
	assert_int_equal(obs_property_get_type(p), OBS_PROPERTY_BOOL);
	assert_true(obs_property_enabled(p));
	assert_true(obs_property_visible(p));

	obs_property_set_enabled(p, false);
	obs_property_set_visible(p, false);
	assert_false(obs_property_enabled(p));
	assert_false(obs_property_visible(p));

	obs_property_set_description(p, "New");
	assert_string_equal(obs_property_description(p), "New");
	/* an empty description becomes NULL */
	obs_property_set_description(p, "");
	assert_null(obs_property_description(p));
	obs_property_set_long_description(p, "Long");
	assert_string_equal(obs_property_long_description(p), "Long");
	obs_property_set_long_description(p, "");
	assert_null(obs_property_long_description(p));

	/* NULL property */
	assert_null(obs_property_name(NULL));
	assert_null(obs_property_description(NULL));
	assert_int_equal(obs_property_get_type(NULL), OBS_PROPERTY_INVALID);
	assert_false(obs_property_enabled(NULL));
	assert_false(obs_property_visible(NULL));

	obs_properties_destroy(props);
}

static void test_order_and_lookup(void **state)
{
	UNUSED_PARAMETER(state);

	obs_properties_t *props = obs_properties_create();
	obs_properties_add_bool(props, "one", "1");
	obs_properties_add_color(props, "two", "2");
	obs_properties_add_color_alpha(props, "three", "3");
	obs_properties_add_font(props, "four", "4");

	/* iteration is insertion order */
	const char *expected[] = {"one", "two", "three", "four"};
	obs_property_t *p = obs_properties_first(props);
	for (size_t i = 0; i < 4; i++) {
		assert_non_null(p);
		assert_string_equal(obs_property_name(p), expected[i]);
		bool more = obs_property_next(&p);
		assert_int_equal(more, i < 3);
	}
	assert_null(p);
	assert_false(obs_property_next(NULL));

	assert_int_equal(obs_property_get_type(obs_properties_get(props, "two")), OBS_PROPERTY_COLOR);
	assert_int_equal(obs_property_get_type(obs_properties_get(props, "three")), OBS_PROPERTY_COLOR_ALPHA);
	assert_int_equal(obs_property_get_type(obs_properties_get(props, "four")), OBS_PROPERTY_FONT);
	assert_null(obs_properties_get(props, "five"));

	/* duplicate names are refused */
	assert_null(obs_properties_add_bool(props, "one", "again"));
	assert_null(obs_properties_add_int(props, "two", "again", 0, 1, 1));

	obs_properties_remove_by_name(props, "two");
	assert_null(obs_properties_get(props, "two"));
	p = obs_properties_first(props);
	assert_string_equal(obs_property_name(p), "one");
	obs_property_next(&p);
	assert_string_equal(obs_property_name(p), "three");
	/* the name is free again */
	assert_non_null(obs_properties_add_bool(props, "two", "back"));
	obs_properties_remove_by_name(props, "missing");
	obs_properties_remove_by_name(NULL, "one");

	obs_properties_destroy(props);
}

static void test_numbers(void **state)
{
	UNUSED_PARAMETER(state);

	obs_properties_t *props = obs_properties_create();
	obs_property_t *i = obs_properties_add_int(props, "i", "I", -5, 10, 2);
	obs_property_t *is = obs_properties_add_int_slider(props, "is", "IS", 0, 100, 5);
	obs_property_t *f = obs_properties_add_float(props, "f", "F", -1.5, 2.5, 0.25);
	obs_property_t *fs = obs_properties_add_float_slider(props, "fs", "FS", 0.0, 1.0, 0.01);

	assert_int_equal(obs_property_int_min(i), -5);
	assert_int_equal(obs_property_int_max(i), 10);
	assert_int_equal(obs_property_int_step(i), 2);
	assert_int_equal(obs_property_int_type(i), OBS_NUMBER_SCROLLER);
	assert_int_equal(obs_property_int_type(is), OBS_NUMBER_SLIDER);
	assert_null(obs_property_int_suffix(i));

	assert_true(obs_property_float_min(f) == -1.5);
	assert_true(obs_property_float_max(f) == 2.5);
	assert_true(obs_property_float_step(f) == 0.25);
	assert_int_equal(obs_property_float_type(f), OBS_NUMBER_SCROLLER);
	assert_int_equal(obs_property_float_type(fs), OBS_NUMBER_SLIDER);
	assert_null(obs_property_float_suffix(f));

	obs_property_int_set_limits(i, 1, 2, 3);
	assert_int_equal(obs_property_int_min(i), 1);
	assert_int_equal(obs_property_int_max(i), 2);
	assert_int_equal(obs_property_int_step(i), 3);
	obs_property_float_set_limits(f, 0.5, 0.75, 0.125);
	assert_true(obs_property_float_min(f) == 0.5);
	assert_true(obs_property_float_max(f) == 0.75);
	assert_true(obs_property_float_step(f) == 0.125);

	obs_property_int_set_suffix(i, " px");
	assert_string_equal(obs_property_int_suffix(i), " px");
	obs_property_int_set_suffix(i, " ms");
	assert_string_equal(obs_property_int_suffix(i), " ms");
	obs_property_int_set_suffix(i, NULL);
	assert_null(obs_property_int_suffix(i));
	obs_property_float_set_suffix(f, " %");
	assert_string_equal(obs_property_float_suffix(f), " %");

	/* getters on the wrong type return defaults; setters do nothing */
	assert_int_equal(obs_property_int_min(f), 0);
	assert_int_equal(obs_property_int_type(f), OBS_NUMBER_SCROLLER);
	assert_true(obs_property_float_max(i) == 0.0);
	obs_property_int_set_limits(f, 7, 8, 9);
	assert_true(obs_property_float_min(f) == 0.5);
	obs_property_float_set_suffix(i, "x");
	assert_null(obs_property_int_suffix(i));

	obs_properties_destroy(props);
}

static void test_text_path_editable(void **state)
{
	UNUSED_PARAMETER(state);

	obs_properties_t *props = obs_properties_create();
	obs_property_t *t = obs_properties_add_text(props, "t", "T", OBS_TEXT_MULTILINE);
	assert_int_equal(obs_property_text_type(t), OBS_TEXT_MULTILINE);
	assert_false(obs_property_text_monospace(t));
	assert_int_equal(obs_property_text_info_type(t), OBS_TEXT_INFO_NORMAL);
	assert_true(obs_property_text_info_word_wrap(t));
	obs_property_text_set_monospace(t, true);
	obs_property_text_set_info_type(t, OBS_TEXT_INFO_ERROR);
	obs_property_text_set_info_word_wrap(t, false);
	assert_true(obs_property_text_monospace(t));
	assert_int_equal(obs_property_text_info_type(t), OBS_TEXT_INFO_ERROR);
	assert_false(obs_property_text_info_word_wrap(t));

	obs_property_t *file = obs_properties_add_path(props, "file", "File", OBS_PATH_FILE, "*.png", "/tmp");
	assert_int_equal(obs_property_path_type(file), OBS_PATH_FILE);
	assert_string_equal(obs_property_path_filter(file), "*.png");
	assert_string_equal(obs_property_path_default_path(file), "/tmp");

	/* characterized, not endorsed: a directory path drops its filter */
	obs_property_t *dir = obs_properties_add_path(props, "dir", "Dir", OBS_PATH_DIRECTORY, "*.png", NULL);
	assert_int_equal(obs_property_path_type(dir), OBS_PATH_DIRECTORY);
	assert_null(obs_property_path_filter(dir));
	assert_null(obs_property_path_default_path(dir));

	obs_property_t *save = obs_properties_add_path(props, "save", "Save", OBS_PATH_FILE_SAVE, NULL, "/out");
	assert_int_equal(obs_property_path_type(save), OBS_PATH_FILE_SAVE);
	assert_null(obs_property_path_filter(save));
	assert_string_equal(obs_property_path_default_path(save), "/out");

	obs_property_t *el =
		obs_properties_add_editable_list(props, "el", "EL", OBS_EDITABLE_LIST_TYPE_FILES, "*.mp4", "/videos");
	assert_int_equal(obs_property_editable_list_type(el), OBS_EDITABLE_LIST_TYPE_FILES);
	assert_string_equal(obs_property_editable_list_filter(el), "*.mp4");
	assert_string_equal(obs_property_editable_list_default_path(el), "/videos");

	/* wrong-type defaults */
	assert_int_equal(obs_property_text_type(file), OBS_TEXT_DEFAULT);
	assert_true(obs_property_text_info_word_wrap(file));
	assert_int_equal(obs_property_path_type(t), OBS_PATH_DIRECTORY);
	assert_int_equal(obs_property_editable_list_type(t), OBS_EDITABLE_LIST_TYPE_STRINGS);
	assert_null(obs_property_editable_list_filter(t));

	obs_properties_destroy(props);
}

static void test_list(void **state)
{
	UNUSED_PARAMETER(state);

	obs_properties_t *props = obs_properties_create();

	/* an editable combo must use the string format */
	assert_null(obs_properties_add_list(props, "bad", "Bad", OBS_COMBO_TYPE_EDITABLE, OBS_COMBO_FORMAT_INT));
	assert_null(obs_properties_get(props, "bad"));

	obs_property_t *s = obs_properties_add_list(props, "s", "S", OBS_COMBO_TYPE_EDITABLE, OBS_COMBO_FORMAT_STRING);
	assert_int_equal(obs_property_list_type(s), OBS_COMBO_TYPE_EDITABLE);
	assert_int_equal(obs_property_list_format(s), OBS_COMBO_FORMAT_STRING);

	assert_int_equal(obs_property_list_add_string(s, "A", "a"), 0);
	assert_int_equal(obs_property_list_add_string(s, "C", "c"), 1);
	obs_property_list_insert_string(s, 1, "B", "b");
	assert_int_equal(obs_property_list_item_count(s), 3);
	assert_string_equal(obs_property_list_item_name(s, 1), "B");
	assert_string_equal(obs_property_list_item_string(s, 1), "b");
	assert_string_equal(obs_property_list_item_string(s, 2), "c");

	/* wrong format: nothing added, 0 returned */
	assert_int_equal(obs_property_list_add_int(s, "X", 5), 0);
	assert_int_equal(obs_property_list_item_count(s), 3);
	assert_int_equal(obs_property_list_item_int(s, 0), 0);

	assert_false(obs_property_list_item_disabled(s, 2));
	obs_property_list_item_disable(s, 2, true);
	assert_true(obs_property_list_item_disabled(s, 2));
	obs_property_list_item_disable(s, 9, true);
	assert_false(obs_property_list_item_disabled(s, 9));

	obs_property_list_item_remove(s, 0);
	obs_property_list_item_remove(s, 9);
	assert_int_equal(obs_property_list_item_count(s), 2);
	assert_string_equal(obs_property_list_item_name(s, 0), "B");
	assert_true(obs_property_list_item_disabled(s, 1));
	assert_null(obs_property_list_item_name(s, 2));
	assert_null(obs_property_list_item_string(s, 2));

	obs_property_list_clear(s);
	assert_int_equal(obs_property_list_item_count(s), 0);
	assert_int_equal(obs_property_list_add_string(s, "Z", "z"), 0);

	obs_property_t *i = obs_properties_add_list(props, "i", "I", OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_INT);
	assert_int_equal(obs_property_list_add_int(i, "big", 1LL << 40), 0);
	obs_property_list_insert_int(i, 0, "neg", -7);
	assert_true(obs_property_list_item_int(i, 0) == -7);
	assert_true(obs_property_list_item_int(i, 1) == (1LL << 40));
	assert_null(obs_property_list_item_string(i, 0));

	obs_property_t *f = obs_properties_add_list(props, "f", "F", OBS_COMBO_TYPE_RADIO, OBS_COMBO_FORMAT_FLOAT);
	assert_int_equal(obs_property_list_add_float(f, "half", 0.5), 0);
	obs_property_list_insert_float(f, 0, "quarter", 0.25);
	assert_true(obs_property_list_item_float(f, 0) == 0.25);
	assert_true(obs_property_list_item_float(f, 1) == 0.5);

	/* characterized, not endorsed: the bool getter reads the union's double
	 * member, which is non-zero exactly when the stored bool is true */
	obs_property_t *b = obs_properties_add_list(props, "b", "B", OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_BOOL);
	assert_int_equal(obs_property_list_add_bool(b, "yes", true), 0);
	assert_int_equal(obs_property_list_add_bool(b, "no", false), 1);
	obs_property_list_insert_bool(b, 0, "first", true);
	assert_true(obs_property_list_item_bool(b, 0));
	assert_true(obs_property_list_item_bool(b, 1));
	assert_false(obs_property_list_item_bool(b, 2));

	/* not a list */
	obs_property_t *nl = obs_properties_add_bool(props, "nl", "NL");
	assert_int_equal(obs_property_list_type(nl), OBS_COMBO_TYPE_INVALID);
	assert_int_equal(obs_property_list_format(nl), OBS_COMBO_FORMAT_INVALID);
	assert_int_equal(obs_property_list_item_count(nl), 0);
	assert_int_equal(obs_property_list_add_string(nl, "A", "a"), 0);

	obs_properties_destroy(props);
}

static void test_frame_rate(void **state)
{
	UNUSED_PARAMETER(state);

	obs_properties_t *props = obs_properties_create();
	obs_property_t *fr = obs_properties_add_frame_rate(props, "fr", "FR");
	struct media_frames_per_second f30 = {30, 1}, f60 = {60, 1}, f24 = {24000, 1001};

	assert_int_equal(obs_property_frame_rate_option_add(fr, "auto", "Auto"), 0);
	assert_int_equal(obs_property_frame_rate_option_add(fr, "max", "Max"), 1);
	obs_property_frame_rate_option_insert(fr, 0, "first", "First");
	assert_int_equal(obs_property_frame_rate_options_count(fr), 3);
	assert_string_equal(obs_property_frame_rate_option_name(fr, 0), "first");
	assert_string_equal(obs_property_frame_rate_option_description(fr, 2), "Max");
	assert_null(obs_property_frame_rate_option_name(fr, 3));

	assert_int_equal(obs_property_frame_rate_fps_range_add(fr, f30, f60), 0);
	obs_property_frame_rate_fps_range_insert(fr, 0, f24, f30);
	assert_int_equal(obs_property_frame_rate_fps_ranges_count(fr), 2);
	assert_int_equal(obs_property_frame_rate_fps_range_min(fr, 0).numerator, 24000);
	assert_int_equal(obs_property_frame_rate_fps_range_min(fr, 0).denominator, 1001);
	assert_int_equal(obs_property_frame_rate_fps_range_max(fr, 1).numerator, 60);
	assert_int_equal(obs_property_frame_rate_fps_range_max(fr, 5).numerator, 0);
	assert_int_equal(obs_property_frame_rate_fps_range_max(fr, 5).denominator, 0);

	obs_property_frame_rate_options_clear(fr);
	assert_int_equal(obs_property_frame_rate_options_count(fr), 0);
	assert_int_equal(obs_property_frame_rate_fps_ranges_count(fr), 2);
	obs_property_frame_rate_fps_ranges_clear(fr);
	assert_int_equal(obs_property_frame_rate_fps_ranges_count(fr), 0);

	obs_property_frame_rate_option_add(fr, "again", "Again");
	obs_property_frame_rate_fps_range_add(fr, f30, f30);
	obs_property_frame_rate_clear(fr);
	assert_int_equal(obs_property_frame_rate_options_count(fr), 0);
	assert_int_equal(obs_property_frame_rate_fps_ranges_count(fr), 0);

	/* not a frame rate */
	obs_property_t *nf = obs_properties_add_bool(props, "nf", "NF");
	assert_true(obs_property_frame_rate_option_add(nf, "x", "X") == DARRAY_INVALID);
	assert_true(obs_property_frame_rate_fps_range_add(nf, f30, f60) == DARRAY_INVALID);
	assert_int_equal(obs_property_frame_rate_options_count(nf), 0);

	obs_properties_destroy(props);
}

static void test_button(void **state)
{
	UNUSED_PARAMETER(state);

	obs_properties_t *props = obs_properties_create();
	int priv;
	obs_property_t *b1 = obs_properties_add_button(props, "b1", "Click", clicked_cb);
	obs_property_t *b2 = obs_properties_add_button2(props, "b2", "Click 2", clicked_cb, &priv);

	assert_string_equal(obs_property_description(b1), "Click");
	assert_int_equal(obs_property_button_type(b1), OBS_BUTTON_DEFAULT);
	/* a button without a URL returns NULL ... */
	assert_null(obs_property_button_url(b1));
	obs_property_button_set_type(b1, OBS_BUTTON_URL);
	obs_property_button_set_url(b1, "https://obsproject.com");
	assert_int_equal(obs_property_button_type(b1), OBS_BUTTON_URL);
	assert_string_equal(obs_property_button_url(b1), "https://obsproject.com");

	/* ... but a non-button returns "" (characterized, not endorsed) */
	obs_property_t *nb = obs_properties_add_bool(props, "nb", "NB");
	assert_string_equal(obs_property_button_url(nb), "");
	assert_int_equal(obs_property_button_type(nb), OBS_BUTTON_DEFAULT);
	assert_false(obs_property_button_clicked(nb, NULL));

	/* without priv, the callback gets the context data; a NULL context
	 * gives NULL */
	reset_calls();
	assert_true(obs_property_button_clicked(b1, NULL));
	assert_int_equal(num_calls, 1);
	assert_string_equal(calls[0].name, "b1");
	assert_ptr_equal(calls[0].props, props);
	assert_null(calls[0].priv);

	/* with priv, the callback gets priv */
	reset_calls();
	assert_true(obs_property_button_clicked(b2, NULL));
	assert_ptr_equal(calls[0].priv, &priv);

	/* no callback */
	obs_property_t *b3 = obs_properties_add_button(props, "b3", "None", NULL);
	assert_false(obs_property_button_clicked(b3, NULL));
	assert_false(obs_property_button_clicked(NULL, NULL));

	obs_properties_destroy(props);
}

static void test_groups(void **state)
{
	UNUSED_PARAMETER(state);

	obs_properties_t *props = obs_properties_create();
	obs_properties_t *inner = obs_properties_create();
	obs_properties_add_bool(props, "top", "Top");
	obs_properties_add_int(inner, "in", "In", 0, 1, 1);

	obs_property_t *g = obs_properties_add_group(props, "g", "G", OBS_GROUP_CHECKABLE, inner);
	assert_non_null(g);
	assert_int_equal(obs_property_get_type(g), OBS_PROPERTY_GROUP);
	assert_int_equal(obs_property_group_type(g), OBS_GROUP_CHECKABLE);
	assert_ptr_equal(obs_property_group_content(g), inner);
	assert_ptr_equal(obs_properties_get_parent(inner), props);
	assert_null(obs_properties_get_parent(props));

	/* lookup reaches into groups */
	obs_property_t *in = obs_properties_get(props, "in");
	assert_non_null(in);
	assert_int_equal(obs_property_get_type(in), OBS_PROPERTY_INT);

	/* names are unique across the whole tree, from either level */
	assert_null(obs_properties_add_bool(props, "in", "dup"));
	assert_null(obs_properties_add_bool(inner, "top", "dup"));
	assert_null(obs_properties_add_bool(inner, "g", "dup"));
	assert_non_null(obs_properties_add_bool(inner, "in2", "In 2"));
	assert_non_null(obs_properties_get(props, "in2"));

	/* a group whose names clash with the parent is refused */
	obs_properties_t *clash = obs_properties_create();
	obs_properties_add_bool(clash, "top", "Top");
	assert_null(obs_properties_add_group(props, "g2", "G2", OBS_GROUP_NORMAL, clash));
	obs_properties_destroy(clash);

	/* recursion and self-groups are refused */
	assert_null(obs_properties_add_group(props, "self", "Self", OBS_GROUP_NORMAL, props));
	assert_null(obs_properties_add_group(props, "null", "Null", OBS_GROUP_NORMAL, NULL));

	/* characterized, not endorsed: a non-group's group type is
	 * OBS_COMBO_INVALID, the list enum's 0 */
	assert_int_equal(obs_property_group_type(obs_properties_get(props, "top")), OBS_COMBO_INVALID);
	assert_null(obs_property_group_content(obs_properties_get(props, "top")));

	/* removal reaches into groups */
	obs_properties_remove_by_name(props, "in");
	assert_null(obs_properties_get(props, "in"));
	assert_non_null(obs_properties_get(inner, "in2"));

	/* removing the group destroys its content and frees its names */
	obs_properties_remove_by_name(props, "g");
	assert_null(obs_properties_get(props, "in2"));
	assert_non_null(obs_properties_add_bool(props, "in2", "free again"));

	obs_properties_destroy(props);
}

static void test_modified_callbacks(void **state)
{
	UNUSED_PARAMETER(state);

	obs_data_t *settings = obs_data_create();
	obs_properties_t *props = obs_properties_create();
	obs_properties_t *inner = obs_properties_create();
	int priv;

	obs_property_t *a = obs_properties_add_bool(props, "a", "A");
	obs_properties_add_bool(props, "plain", "Plain");
	obs_property_t *in = obs_properties_add_bool(inner, "in", "In");
	obs_property_t *g = obs_properties_add_group(props, "g", "G", OBS_GROUP_NORMAL, inner);
	obs_property_t *z = obs_properties_add_bool(props, "z", "Z");

	obs_property_set_modified_callback(a, modified_cb);
	obs_property_set_modified_callback2(in, modified2_cb, &priv);
	obs_property_set_modified_callback(g, modified_cb);
	obs_property_set_modified_callback2(z, modified2_cb, &priv);

	/* obs_property_modified returns the callback's result and passes the
	 * topmost properties, also for a property inside a group */
	reset_calls();
	assert_true(obs_property_modified(a, settings));
	assert_false(obs_property_modified(in, settings));
	assert_false(obs_property_modified(obs_properties_get(props, "plain"), settings));
	assert_false(obs_property_modified(NULL, settings));
	assert_int_equal(num_calls, 2);
	assert_ptr_equal(calls[0].props, props);
	assert_ptr_equal(calls[1].props, props);
	assert_ptr_equal(calls[1].priv, &priv);

	/* characterized, not endorsed: apply_settings collects a group's
	 * content before the group itself, then calls back in reverse */
	reset_calls();
	obs_properties_apply_settings(props, settings);
	const char *order[] = {"z", "g", "in", "a"};
	assert_int_equal(num_calls, 4);
	for (size_t i = 0; i < 4; i++)
		assert_string_equal(calls[i].name, order[i]);

	/* the callbacks get the properties apply_settings was called on, not
	 * the topmost ones */
	reset_calls();
	obs_properties_apply_settings(inner, settings);
	assert_int_equal(num_calls, 1);
	assert_string_equal(calls[0].name, "in");
	assert_ptr_equal(calls[0].props, inner);

	/* modified takes precedence over modified2 */
	obs_property_set_modified_callback(z, modified_cb);
	reset_calls();
	assert_true(obs_property_modified(z, settings));
	assert_null(calls[0].priv);

	obs_properties_apply_settings(NULL, settings);

	obs_properties_destroy(props);
	obs_data_release(settings);
}

int main(void)
{
	const struct CMUnitTest tests[] = {
		cmocka_unit_test(test_create_param_flags),
		cmocka_unit_test(test_common_fields),
		cmocka_unit_test(test_order_and_lookup),
		cmocka_unit_test(test_numbers),
		cmocka_unit_test(test_text_path_editable),
		cmocka_unit_test(test_list),
		cmocka_unit_test(test_frame_rate),
		cmocka_unit_test(test_button),
		cmocka_unit_test(test_groups),
		cmocka_unit_test(test_modified_callbacks),
	};

	return cmocka_run_group_tests(tests, NULL, NULL);
}
