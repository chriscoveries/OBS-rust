/* SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once

#include <stdbool.h>
#include <stdint.h>

/* Private encoder settings used for dynamic VideoToolbox rate updates. */
struct vt_rate_state {
	uint32_t bitrate;
	bool limit_bitrate;
	uint32_t max_bitrate;
	double max_bitrate_window;
	float quality;
};

static inline bool vt_rate_state_changed(const struct vt_rate_state *old, const struct vt_rate_state *current)
{
	return old->bitrate != current->bitrate || old->limit_bitrate != current->limit_bitrate ||
	       old->max_bitrate != current->max_bitrate ||
	       old->max_bitrate_window != current->max_bitrate_window || old->quality != current->quality;
}
