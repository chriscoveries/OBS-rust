/* Wayland hotkeys held while OBS is focused (upstream
 * obsproject/obs-studio#10701).
 *
 * The real libobs hotkey thread (obs-hotkey.c, compiled in with an isolated
 * core) polls obs_hotkeys_platform_is_pressed(), which this test answers from
 * obs_nix_hotkey_state as the Wayland platform does. No Wayland display is
 * needed: the test plays the part of the wl_keyboard listener and of the Qt
 * key filter that injects presses. */

#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <cmocka.h>
#include <stdio.h>

#include <obs-internal.h>
#include <util/profiler.h>

#include "obs-nix-hotkey-state.h"

struct obs_core *hotkey_test_obs;

static struct obs_nix_hotkey_state key_state;
static pthread_t hotkey_thread;
static profiler_name_store_t *name_store;

bool obs_hotkeys_platform_is_pressed(obs_hotkeys_platform_t *context, obs_key_t key)
{
	UNUSED_PARAMETER(context);
	return obs_nix_hotkey_state_is_pressed(&key_state, key);
}

/* obs_hotkey_thread() names its profiler entry through the core; the real
 * core is not running here. */
profiler_name_store_t *obs_get_profiler_name_store(void)
{
	return name_store;
}

struct hotkey_log {
	volatile long pressed;
	volatile long presses;
	volatile long releases;
};

static void hotkey_callback(void *data, obs_hotkey_id id, obs_hotkey_t *hotkey, bool pressed)
{
	UNUSED_PARAMETER(id);
	UNUSED_PARAMETER(hotkey);
	struct hotkey_log *log = data;
	os_atomic_set_long(&log->pressed, pressed);
	if (pressed)
		os_atomic_inc_long(&log->presses);
	else
		os_atomic_inc_long(&log->releases);
}

static int setup(void **state)
{
	UNUSED_PARAMETER(state);
	memset(&key_state, 0, sizeof(key_state));
	name_store = profiler_name_store_create();
	hotkey_test_obs = bzalloc(sizeof(*hotkey_test_obs));
	assert_int_equal(pthread_mutex_init_recursive(&hotkey_test_obs->hotkeys.mutex), 0);
	assert_int_equal(os_event_init(&hotkey_test_obs->hotkeys.stop_event, OS_EVENT_TYPE_MANUAL), 0);
	hotkey_test_obs->hotkeys.strict_modifiers = true;
	hotkey_test_obs->hotkeys.signals = signal_handler_create();
	static const char *signals[] = {"void hotkey_register(ptr key)", "void hotkey_unregister(ptr key)",
					"void hotkey_bindings_changed(ptr key)", NULL};
	assert_true(signal_handler_add_array(hotkey_test_obs->hotkeys.signals, signals));
	assert_int_equal(pthread_create(&hotkey_thread, NULL, obs_hotkey_thread, NULL), 0);
	return 0;
}

static int teardown(void **state)
{
	UNUSED_PARAMETER(state);
	os_event_signal(hotkey_test_obs->hotkeys.stop_event);
	pthread_join(hotkey_thread, NULL);
	os_event_destroy(hotkey_test_obs->hotkeys.stop_event);
	obs_hotkeys_free();
	signal_handler_destroy(hotkey_test_obs->hotkeys.signals);
	pthread_mutex_destroy(&hotkey_test_obs->hotkeys.mutex);
	bfree(hotkey_test_obs);
	hotkey_test_obs = NULL;
	profiler_name_store_free(name_store);
	name_store = NULL;
	return 0;
}

static obs_hotkey_id register_bound(const char *name, struct hotkey_log *log, obs_key_combination_t combo)
{
	obs_hotkey_id id = obs_hotkey_register_frontend(name, name, hotkey_callback, log);
	assert_int_not_equal(id, OBS_INVALID_HOTKEY_ID);
	obs_hotkey_load_bindings(id, &combo, 1);
	return id;
}

/* Several hotkey thread cycles (25 ms each). */
static void wait_cycles(void)
{
	os_sleep_ms(150);
}

static bool wait_pressed(struct hotkey_log *log, bool pressed)
{
	for (int i = 0; i < 100; i++) {
		if ((os_atomic_load_long(&log->pressed) != 0) == pressed)
			return true;
		os_sleep_ms(10);
	}
	return false;
}

static void expect_pressed(struct hotkey_log *log, bool pressed, const char *what)
{
	if (!wait_pressed(log, pressed)) {
		fprintf(stderr, "%s: hotkey %s (presses %ld, releases %ld)\n", what,
			pressed ? "never pressed" : "never released", os_atomic_load_long(&log->presses),
			os_atomic_load_long(&log->releases));
		fail();
	}
}

static const obs_key_combination_t f13 = {0, OBS_KEY_F13};
static const obs_key_combination_t ctrl_shift_f13 = {INTERACT_CONTROL_KEY | INTERACT_SHIFT_KEY, OBS_KEY_F13};

/* Upstream #10701: with OBS focused, Qt injects the press, but the hotkey
 * thread released push-to-talk within 25 ms because nothing reported the key
 * as held. */
static void focused_key_holds_push_to_talk(void **state)
{
	UNUSED_PARAMETER(state);
	struct hotkey_log ptt = {0};
	register_bound("ptt", &ptt, f13);

	obs_nix_hotkey_state_set_key(&key_state, OBS_KEY_F13, true);
	obs_hotkey_inject_event(f13, true);
	expect_pressed(&ptt, true, "focused press");
	wait_cycles();
	if (os_atomic_load_long(&ptt.releases) != 0) {
		fprintf(stderr, "focused push-to-talk released while the key is held (releases %ld)\n",
			os_atomic_load_long(&ptt.releases));
		fail();
	}

	obs_nix_hotkey_state_set_key(&key_state, OBS_KEY_F13, false);
	expect_pressed(&ptt, false, "focused release");
}

/* Focused modifiers come from the keyboard's modifier state. */
static void focused_modifiers_hold_combination(void **state)
{
	UNUSED_PARAMETER(state);
	struct hotkey_log combo = {0};
	register_bound("combo", &combo, ctrl_shift_f13);

	obs_nix_hotkey_state_set_modifiers(&key_state, INTERACT_CONTROL_KEY | INTERACT_SHIFT_KEY);
	obs_nix_hotkey_state_set_key(&key_state, OBS_KEY_F13, true);
	obs_hotkey_inject_event(ctrl_shift_f13, true);
	expect_pressed(&combo, true, "focused ctrl+shift+F13");
	wait_cycles();
	assert_int_equal(os_atomic_load_long(&combo.releases), 0);

	obs_nix_hotkey_state_set_modifiers(&key_state, INTERACT_CONTROL_KEY);
	expect_pressed(&combo, false, "shift released");
}

/* Losing keyboard focus releases what was held, as no release event will
 * arrive for it. */
static void focus_loss_releases_keys(void **state)
{
	UNUSED_PARAMETER(state);
	struct hotkey_log ptt = {0};
	register_bound("ptt", &ptt, f13);

	obs_nix_hotkey_state_set_key(&key_state, OBS_KEY_F13, true);
	obs_hotkey_inject_event(f13, true);
	expect_pressed(&ptt, true, "focused press");

	obs_nix_hotkey_state_clear_keys(&key_state);
	expect_pressed(&ptt, false, "focus lost");
}

/* Nothing reported: as before, the hotkey thread releases an injected press
 * whose key it cannot see. */
static void nothing_reported_releases(void **state)
{
	UNUSED_PARAMETER(state);
	struct hotkey_log ptt = {0};
	register_bound("ptt", &ptt, f13);

	obs_hotkey_inject_event(f13, true);
	assert_int_equal(os_atomic_load_long(&ptt.presses), 1);
	expect_pressed(&ptt, false, "unreported key");
	for (obs_key_t key = 0; key < OBS_KEY_LAST_VALUE; key++)
		assert_false(obs_nix_hotkey_state_is_pressed(&key_state, key));
}

int main(void)
{
	const struct CMUnitTest tests[] = {
		cmocka_unit_test_setup_teardown(focused_key_holds_push_to_talk, setup, teardown),
		cmocka_unit_test_setup_teardown(focused_modifiers_hold_combination, setup, teardown),
		cmocka_unit_test_setup_teardown(focus_loss_releases_keys, setup, teardown),
		cmocka_unit_test_setup_teardown(nothing_reported_releases, setup, teardown),
	};
	return cmocka_run_group_tests(tests, NULL, NULL);
}
