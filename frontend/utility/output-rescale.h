#pragma once

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Settings > Output > Advanced: the "Rescale Output" filter box and the
 * resolution box next to it, kept free of Qt so they can be unit tested.
 * Settings calls these when it loads the Streaming tab, when the filter
 * changes, and when multitrack video (Enhanced Broadcasting) locks or unlocks
 * the stream encoder settings. */
struct output_rescale_ops {
	void *param;

	/* Whether the filter box has a filter selected (not "Disabled"). */
	bool (*rescales)(void *param);

	/* Whether the filter box can be used. */
	bool (*filter_enabled)(void *param);

	void (*set_filter_enabled)(void *param, bool enabled);
	void (*set_res_enabled)(void *param, bool enabled);
};

/* The filter box changed (or was loaded). The resolution box is enabled
 * when a filter is selected and the filter box is not locked. */
void output_rescale_filter_changed(const struct output_rescale_ops *ops);

/* Multitrack video locked (or unlocked) the stream encoder settings: locks
 * the filter box, and the resolution box follows both. */
void output_rescale_set_locked(const struct output_rescale_ops *ops, bool locked);

#ifdef __cplusplus
}
#endif
