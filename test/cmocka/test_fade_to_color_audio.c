/* Fade-to-color gain callbacks must remain finite at endpoint switch points.
 * Include the unchanged plugin to exercise its static callbacks headlessly,
 * without exporting test hooks or replacing any libobs functions. */

#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <cmocka.h>

#include <math.h>

#include "../../plugins/obs-transitions/transition-fade-to-color.c"

OBS_DECLARE_MODULE()

const char *obs_module_text(const char *text)
{
	return text;
}

static void check_switch_point(int switch_point, const float expected_a[5], const float expected_b[5])
{
	struct fade_to_color_info fade = {0};
	obs_data_t *settings = obs_data_create();
	assert_non_null(settings);
	obs_data_set_int(settings, "switch_point", switch_point);
	fade_to_color_transition.update(&fade, settings);
	obs_data_release(settings);

	obs_transition_audio_mix_callback_t gain_a = mix_a;
	obs_transition_audio_mix_callback_t gain_b = mix_b;

	for (size_t i = 0; i < 5; i++) {
		float t = (float)i / 4.0f;
		float a = gain_a(&fade, t);
		float b = gain_b(&fade, t);
		/* A NaN gain poisons even an unrelated source in the summed mix.
		 * At the color boundary, that source must still be audible. */
		float mixed = 0.125f + 0.25f * a + 0.5f * b;
		print_message("switch_point=%d t=%.2f: A=%g B=%g mix=%g\n", switch_point, t, a, b, mixed);
		assert_true(isfinite(a));
		assert_true(isfinite(b));
		assert_true(isfinite(mixed));
		assert_true(mixed >= 0.125f);
		assert_float_equal(a, expected_a[i], 0.000001f);
		assert_float_equal(b, expected_b[i], 0.000001f);
		assert_float_equal(mixed, 0.125f + 0.25f * expected_a[i] + 0.5f * expected_b[i], 0.000001f);
	}
}

static void switch_point_zero(void **state)
{
	UNUSED_PARAMETER(state);
	const float a[5] = {0.0f, 0.0f, 0.0f, 0.0f, 0.0f};
	const float b[5] = {0.0f, 0.0390625f, 0.25f, 0.6328125f, 1.0f};
	check_switch_point(0, a, b);
}

static void switch_point_half(void **state)
{
	UNUSED_PARAMETER(state);
	const float a[5] = {1.0f, 0.375f, 0.0f, 0.0f, 0.0f};
	const float b[5] = {0.0f, 0.0f, 0.0f, 0.375f, 1.0f};
	check_switch_point(50, a, b);
}

static void switch_point_full(void **state)
{
	UNUSED_PARAMETER(state);
	const float a[5] = {1.0f, 0.6328125f, 0.25f, 0.0390625f, 0.0f};
	const float b[5] = {0.0f, 0.0f, 0.0f, 0.0f, 1.0f};
	check_switch_point(100, a, b);
}

int main(void)
{
	const struct CMUnitTest tests[] = {
		cmocka_unit_test(switch_point_zero),
		cmocka_unit_test(switch_point_half),
		cmocka_unit_test(switch_point_full),
	};
	return cmocka_run_group_tests(tests, NULL, NULL);
}
