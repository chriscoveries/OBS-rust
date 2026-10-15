/* SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once

/* Private recording profile. This is not part of the public libobs ABI.
 * Keep the scalar reference and hdr-recording.effect mathematically aligned.
 * Values entering the curve are display-linear BT.2020 RGB, in nits. */
#include <stdbool.h>
#include <math.h>
#include <string.h>

enum hdr_recording_rolloff { HDR_RECORDING_SOFT = 0, HDR_RECORDING_HARD = 1 };

struct hdr_recording_profile {
	bool enabled;
	int rolloff;
	float exposure_ev;
	float white_nits;
	float peak_nits;
	float knee_fraction;
};

static inline bool hdr_recording_profile_init(struct hdr_recording_profile *out, bool enabled, int rolloff,
					      double exposure_ev, double white_nits, double peak_nits,
					      double knee_percent)
{
	if (!out)
		return false;
	memset(out, 0, sizeof(*out));
	if (!enabled)
		return true;
	if ((rolloff != HDR_RECORDING_SOFT && rolloff != HDR_RECORDING_HARD) || !isfinite(exposure_ev) ||
	    !isfinite(white_nits) || !isfinite(peak_nits) || !isfinite(knee_percent) || exposure_ev < -4.0 ||
	    exposure_ev > 4.0 || white_nits < 80.0 || white_nits > 400.0 || peak_nits < 400.0 ||
	    peak_nits > 2000.0 || white_nits >= peak_nits || knee_percent < 50.0 || knee_percent > 95.0)
		return false;
	out->enabled = true;
	out->rolloff = rolloff;
	out->exposure_ev = (float)exposure_ev;
	out->white_nits = (float)white_nits;
	out->peak_nits = (float)peak_nits;
	out->knee_fraction = (float)(knee_percent * 0.01);
	return true;
}

static inline bool hdr_recording_profile_equal(const struct hdr_recording_profile *a,
					       const struct hdr_recording_profile *b)
{
	if (a->enabled != b->enabled)
		return false;
	if (!a->enabled)
		return true;
	/* Avoid padding bytes in memcmp and compare the complete immutable key. */
	return a->rolloff == b->rolloff && a->exposure_ev == b->exposure_ev && a->white_nits == b->white_nits &&
	       a->peak_nits == b->peak_nits && a->knee_fraction == b->knee_fraction;
}

static inline double hdr_recording_map_level(double nits, const struct hdr_recording_profile *p)
{
	(void)p;
	return nits; /* RED: highlight mapping not implemented. */
}

static inline void hdr_recording_grade_nits(float rgb[3], const struct hdr_recording_profile *p)
{
	(void)rgb;
	(void)p; /* RED: recording grade not implemented. */
}
