#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <stdbool.h>
#include <stdint.h>
#include <cmocka.h>

#include "output-rescale.h"

/* Settings > Output > Advanced > Streaming: with "Rescale Output" set to
 * "Disabled", the resolution box next to it must be greyed out, as it is on
 * the Recording tab. It was enabled every time Settings opened, because the
 * multitrack video (Enhanced Broadcasting) update, which runs on every open
 * and every stream service change, enabled it without looking at the filter.
 *
 * The model below stands in for the two QComboBoxes of one Output tab. */

struct rescale_model {
	bool rescales;       /* filter box: a filter is selected (not "Disabled") */
	bool filter_enabled; /* filter box: QWidget::isEnabled() */
	bool res_enabled;    /* resolution box: QWidget::isEnabled() */
	struct output_rescale_ops ops;
};

static bool model_rescales(void *param)
{
	return ((struct rescale_model *)param)->rescales;
}

static bool model_filter_enabled(void *param)
{
	return ((struct rescale_model *)param)->filter_enabled;
}

static void model_set_filter_enabled(void *param, bool enabled)
{
	((struct rescale_model *)param)->filter_enabled = enabled;
}

static void model_set_res_enabled(void *param, bool enabled)
{
	((struct rescale_model *)param)->res_enabled = enabled;
}

/* A freshly loaded tab: the filter is set from the profile and the
 * resolution box follows it (LoadAdvOutputStreamingSettings). */
static void model_load(struct rescale_model *m, bool rescales)
{
	m->rescales = rescales;
	m->filter_enabled = true;
	m->res_enabled = true;
	m->ops.param = m;
	m->ops.rescales = model_rescales;
	m->ops.filter_enabled = model_filter_enabled;
	m->ops.set_filter_enabled = model_set_filter_enabled;
	m->ops.set_res_enabled = model_set_res_enabled;
	output_rescale_filter_changed(&m->ops);
}

/* Settings opens with Rescale Output "Disabled" and no multitrack video:
 * the multitrack update runs (unlocked) after the load. */
static void disabled_filter_greys_res_on_open(void **state)
{
	(void)state;
	struct rescale_model m;
	model_load(&m, false);
	assert_false(m.res_enabled);

	output_rescale_set_locked(&m.ops, false);
	assert_true(m.filter_enabled);
	assert_false(m.res_enabled);
}

/* Turning multitrack video on and off again keeps the resolution box greyed
 * out while the filter is "Disabled". */
static void disabled_filter_stays_grey_after_unlock(void **state)
{
	(void)state;
	struct rescale_model m;
	model_load(&m, false);

	output_rescale_set_locked(&m.ops, true);
	assert_false(m.filter_enabled);
	assert_false(m.res_enabled);

	output_rescale_set_locked(&m.ops, false);
	assert_true(m.filter_enabled);
	assert_false(m.res_enabled);
}

/* With a filter selected the resolution box can be used, unless multitrack
 * video owns the encoder settings. */
static void selected_filter_enables_res_unless_locked(void **state)
{
	(void)state;
	struct rescale_model m;
	model_load(&m, true);
	assert_true(m.res_enabled);

	output_rescale_set_locked(&m.ops, false);
	assert_true(m.res_enabled);

	output_rescale_set_locked(&m.ops, true);
	assert_false(m.filter_enabled);
	assert_false(m.res_enabled);

	output_rescale_set_locked(&m.ops, false);
	assert_true(m.filter_enabled);
	assert_true(m.res_enabled);
}

/* Choosing a filter enables the resolution box, choosing "Disabled" greys it
 * out again. */
static void filter_change_follows_selection(void **state)
{
	(void)state;
	struct rescale_model m;
	model_load(&m, false);

	m.rescales = true;
	output_rescale_filter_changed(&m.ops);
	assert_true(m.res_enabled);

	m.rescales = false;
	output_rescale_filter_changed(&m.ops);
	assert_false(m.res_enabled);
}

/* While multitrack video has the filter box locked, a filter loaded from
 * the profile must not enable the resolution box. */
static void filter_change_while_locked_keeps_res_grey(void **state)
{
	(void)state;
	struct rescale_model m;
	model_load(&m, false);
	output_rescale_set_locked(&m.ops, true);

	m.rescales = true;
	output_rescale_filter_changed(&m.ops);
	assert_false(m.res_enabled);
}

int main(void)
{
	const struct CMUnitTest tests[] = {
		cmocka_unit_test(disabled_filter_greys_res_on_open),
		cmocka_unit_test(disabled_filter_stays_grey_after_unlock),
		cmocka_unit_test(selected_filter_enables_res_unless_locked),
		cmocka_unit_test(filter_change_follows_selection),
		cmocka_unit_test(filter_change_while_locked_keeps_res_grey),
	};

	return cmocka_run_group_tests(tests, NULL, NULL);
}
