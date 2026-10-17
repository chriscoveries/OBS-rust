/* Test-only oracle: the original libobs/obs-hotkey-name-map.c, unmodified,
 * with its exports renamed to oracle_* so it can link next to the Rust
 * implementation. The libobs core global `obs` is renamed too and owned
 * here: oracle_hotkey_name_map_set_core switches between no core (the
 * linear-scan fallback) and a zeroed core (the name map), so parity covers
 * both C paths. */
#ifndef _WIN32
#define _GNU_SOURCE
#endif
#define obs_key_to_name oracle_obs_key_to_name
#define obs_key_from_name oracle_obs_key_from_name
#define obs_hotkey_name_map_free oracle_obs_hotkey_name_map_free
#define obs oracle_hotkey_obs

/* obs_key_from_name returns an int map value as obs_key_t; clang treats the
 * enum as unsigned and the oracle builds with -Wsign-conversion. libobs
 * does not. */
#if defined(__clang__) || defined(__GNUC__)
#pragma GCC diagnostic ignored "-Wsign-conversion"
#endif

#include "obs-hotkey-name-map.c.h"

struct obs_core *oracle_hotkey_obs = NULL;

void oracle_hotkey_name_map_set_core(bool on);

void oracle_hotkey_name_map_set_core(bool on)
{
	if (oracle_hotkey_obs) {
		oracle_obs_hotkey_name_map_free();
		bfree(oracle_hotkey_obs);
		oracle_hotkey_obs = NULL;
	}
	if (on) {
		pthread_once_t once = PTHREAD_ONCE_INIT;
		oracle_hotkey_obs = bzalloc(sizeof(struct obs_core));
		oracle_hotkey_obs->hotkeys.name_map_init_token = once;
	}
}
