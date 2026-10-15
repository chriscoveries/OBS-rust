/* SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once
#include "hdr-recording-profile.h"

/* The shared conversion effect must be reset for every video mix, including
 * an ordinary SDR mix immediately after an HDR recording mix. */
static inline void hdr_recording_set_effect(gs_effect_t *effect, const struct hdr_recording_profile *p)
{
	gs_effect_set_float(gs_effect_get_param_by_name(effect, "hdr_recording_enabled"), p->enabled ? 1.0f : 0.0f);
	gs_effect_set_float(gs_effect_get_param_by_name(effect, "hdr_recording_exposure"),
			    p->enabled ? exp2f(p->exposure_ev) : 1.0f);
	gs_effect_set_float(gs_effect_get_param_by_name(effect, "hdr_recording_peak"), p->enabled ? p->peak_nits : 1000.0f);
	gs_effect_set_float(gs_effect_get_param_by_name(effect, "hdr_recording_knee"), p->enabled ? p->knee_fraction : 0.8f);
	gs_effect_set_float(gs_effect_get_param_by_name(effect, "hdr_recording_soft"),
			    p->enabled && p->rolloff == HDR_RECORDING_HARD ? 0.0f : 1.0f);
}
