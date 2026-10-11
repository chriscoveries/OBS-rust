/*
This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#include "obs-nix-hotkey-state.h"

/* Changes take obs->hotkeys.mutex so they land between two poll cycles of
 * the hotkey thread, which holds it for a whole cycle. */
static inline void lock(void)
{
	pthread_mutex_lock(&obs->hotkeys.mutex);
}

static inline void unlock(void)
{
	pthread_mutex_unlock(&obs->hotkeys.mutex);
}

void obs_nix_hotkey_state_set_key(struct obs_nix_hotkey_state *state, obs_key_t key, bool pressed)
{
	if (key <= OBS_KEY_NONE || key >= OBS_KEY_LAST_VALUE)
		return;

	lock();
	state->focused_keys[key] = pressed;
	unlock();
}

void obs_nix_hotkey_state_set_modifiers(struct obs_nix_hotkey_state *state, uint32_t modifiers)
{
	lock();
	state->focused_modifiers = modifiers;
	unlock();
}

void obs_nix_hotkey_state_clear_keys(struct obs_nix_hotkey_state *state)
{
	lock();
	memset(state->focused_keys, 0, sizeof(state->focused_keys));
	state->focused_modifiers = 0;
	unlock();
}

static inline uint32_t modifier_for_key(obs_key_t key)
{
	switch (key) {
	case OBS_KEY_SHIFT:
		return INTERACT_SHIFT_KEY;
	case OBS_KEY_CONTROL:
		return INTERACT_CONTROL_KEY;
	case OBS_KEY_ALT:
		return INTERACT_ALT_KEY;
	case OBS_KEY_META:
		return INTERACT_COMMAND_KEY;
	default:
		return 0;
	}
}

bool obs_nix_hotkey_state_is_pressed(struct obs_nix_hotkey_state *state, obs_key_t key)
{
	if (key <= OBS_KEY_NONE || key >= OBS_KEY_LAST_VALUE)
		return false;

	uint32_t modifier = modifier_for_key(key);
	bool pressed;

	lock();
	pressed = modifier ? (state->focused_modifiers & modifier) != 0 : state->focused_keys[key];
	unlock();
	return pressed;
}
