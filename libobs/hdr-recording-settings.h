/* SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once
#include "obs-data.h"
#include "hdr-recording-profile.h"

#define HDR_RECORDING_ENABLE "hdr_recording"
/* Snapshot written only during encoder initialization, never by the UI. */
#define HDR_RECORDING_ACTIVE "_hdr_recording_active"
#define HDR_RECORDING_EXPOSURE "hdr_recording_exposure"
#define HDR_RECORDING_WHITE "hdr_recording_white_nits"
#define HDR_RECORDING_PEAK "hdr_recording_peak_nits"
#define HDR_RECORDING_KNEE "hdr_recording_knee_percent"
#define HDR_RECORDING_ROLLOFF "hdr_recording_rolloff"

static inline double hdr_recording_number(obs_data_t *s, const char *key, double fallback)
{
	return obs_data_has_user_value(s, key) || obs_data_has_default_value(s, key) ? obs_data_get_double(s, key)
											 : fallback;
}

static inline bool hdr_recording_read(obs_data_t *s, struct hdr_recording_profile *out)
{
	const bool enabled = obs_data_get_bool(s, HDR_RECORDING_ENABLE);
	const long long rolloff = obs_data_get_int(s, HDR_RECORDING_ROLLOFF);
	if (enabled && rolloff != HDR_RECORDING_SOFT && rolloff != HDR_RECORDING_HARD)
		return false;
	return hdr_recording_profile_init(out, enabled, enabled ? (int)rolloff : HDR_RECORDING_SOFT,
					 hdr_recording_number(s, HDR_RECORDING_EXPOSURE, 0.0),
					 hdr_recording_number(s, HDR_RECORDING_WHITE, 203.0),
					 hdr_recording_number(s, HDR_RECORDING_PEAK, 1000.0),
					 hdr_recording_number(s, HDR_RECORDING_KNEE, 80.0));
}

static inline void hdr_recording_defaults(obs_data_t *s)
{
	obs_data_set_default_bool(s, HDR_RECORDING_ENABLE, false);
	obs_data_set_default_int(s, HDR_RECORDING_ROLLOFF, HDR_RECORDING_SOFT);
	obs_data_set_default_double(s, HDR_RECORDING_EXPOSURE, 0.0);
	obs_data_set_default_double(s, HDR_RECORDING_WHITE, 203.0);
	obs_data_set_default_double(s, HDR_RECORDING_PEAK, 1000.0);
	obs_data_set_default_double(s, HDR_RECORDING_KNEE, 80.0);
}
