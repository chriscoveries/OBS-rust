#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <setjmp.h>
#include <cmocka.h>

#include <obs-properties.h>
#include <util/dstr.h>

#include "v4l2-framerate.h"
#include "v4l2-helpers.h"

/* Picking 30 fps for a v4l2 source puts the device at its fastest rate
 * instead (60 fps in the report, obsproject/obs-studio#13388, by acscd), and
 * every change to a camera control such as zoom or focus restarts the stream
 * (#13896, by shanefagan).
 *
 * The framerate list offers each discrete frame interval as a packed 64-bit
 * (numerator, denominator) tuple, but stored it through an int, keeping only
 * the denominator. The saved setting is then 30 for 1/30 s: unpacked as a
 * legacy 16-bit tuple that is 0/30 s per frame, an infinite rate the driver
 * clamps to its maximum. And because the device reports the real packed
 * interval back, the stored setting never equals it, so every settings
 * update looks like a framerate change and restarts the capture. */

static obs_property_t *framerate_list(obs_properties_t *props)
{
	return obs_properties_add_list(props, "framerate", "Frame Rate", OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_INT);
}

/* The value the source settings get when the user picks the discrete
 * interval numerator/denominator from the list. */
static int64_t picked_framerate(uint32_t numerator, uint32_t denominator)
{
	obs_properties_t *props = obs_properties_create();
	obs_property_t *prop = framerate_list(props);
	struct dstr buffer = {0};

	v4l2_framerate_list_add_discrete(prop, &buffer, numerator, denominator);
	int64_t value = obs_property_list_item_int(prop, 0);

	dstr_free(&buffer);
	obs_properties_destroy(props);
	return value;
}

static void picked_framerate_requests_that_interval(void **state)
{
	(void)state;

	static const uint32_t intervals[][2] = {{1, 30}, {1, 60}, {1, 15}, {1001, 30000}};

	for (size_t i = 0; i < sizeof(intervals) / sizeof(intervals[0]); i++) {
		int64_t value = picked_framerate(intervals[i][0], intervals[i][1]);
		int num = 0, denom = 0;

		/* what v4l2_set_framerate() passes to VIDIOC_S_PARM */
		v4l2_unpack_tuple(&num, &denom, value);
		if ((uint32_t)num != intervals[i][0] || (uint32_t)denom != intervals[i][1]) {
			fprintf(stderr, "picked %u/%u s per frame, setting %lld requests %d/%d\n", intervals[i][0],
				intervals[i][1], (long long)value, num, denom);
			fail();
		}
	}
}

static void control_change_does_not_restart_stream(void **state)
{
	(void)state;

	int64_t setting = picked_framerate(1, 30);
	/* the device accepted the interval and v4l2_set_framerate() reported it */
	int64_t running = v4l2_pack_tuple(1, 30);
	bool unchanged = false;

	/* any settings update, e.g. moving the zoom slider */
	if (v4l2_framerate_changed(running, setting, &unchanged)) {
		fprintf(stderr, "running at %lld, setting %lld: restart on an unrelated change\n", (long long)running,
			(long long)setting);
		fail();
	}
}

static void leave_unchanged_restarts_once(void **state)
{
	(void)state;

	int64_t running = v4l2_pack_tuple(1, 30);
	bool unchanged = false;

	assert_true(v4l2_framerate_changed(running, -1, &unchanged));
	assert_false(v4l2_framerate_changed(running, -1, &unchanged));
	assert_true(v4l2_framerate_changed(running, v4l2_pack_tuple(1, 60), &unchanged));
	assert_false(v4l2_framerate_changed(running, running, &unchanged));
}

int main(void)
{
	const struct CMUnitTest tests[] = {
		cmocka_unit_test(picked_framerate_requests_that_interval),
		cmocka_unit_test(control_change_does_not_restart_stream),
		cmocka_unit_test(leave_unchanged_restarts_once),
	};

	return cmocka_run_group_tests(tests, NULL, NULL);
}
