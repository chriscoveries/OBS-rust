/* SPDX-License-Identifier: GPL-2.0-or-later */
/* GPU-independent tests of the exact production profile and its scalar curve.
 * The GPU shader and encoded output still require native integration tests. */
#include "../../libobs/hdr-recording-profile.h"
#include <stdio.h>
#include <stdlib.h>
#include <float.h>

#define CHECK(x) do { if (!(x)) { fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #x); exit(1); } } while (0)

int main(void)
{
	struct hdr_recording_profile p, q;
	CHECK(hdr_recording_profile_init(&p, true, 0, 0, 203, 1000, 80));
	q = p;
	CHECK(hdr_recording_profile_equal(&p, &q));
	q.exposure_ev = 1; CHECK(!hdr_recording_profile_equal(&p, &q)); q = p;
	q.white_nits = 100; CHECK(!hdr_recording_profile_equal(&p, &q)); q = p;
	q.peak_nits = 600; CHECK(!hdr_recording_profile_equal(&p, &q)); q = p;
	q.knee_fraction = .5f; CHECK(!hdr_recording_profile_equal(&p, &q)); q = p;
	q.rolloff = 1; CHECK(!hdr_recording_profile_equal(&p, &q)); q = p;
	q.enabled = false; CHECK(!hdr_recording_profile_equal(&p, &q));

	CHECK(!hdr_recording_profile_init(&p, true, 2, 0, 203, 1000, 80));
	CHECK(!hdr_recording_profile_init(&p, true, 0, NAN, 203, 1000, 80));
	CHECK(!hdr_recording_profile_init(&p, true, 0, 0, INFINITY, 1000, 80));
	CHECK(!hdr_recording_profile_init(&p, true, 0, 0, 203, INFINITY, 80));
	CHECK(!hdr_recording_profile_init(&p, true, 0, 0, 203, 1000, NAN));
	CHECK(!hdr_recording_profile_init(&p, true, 0, 5, 203, 1000, 80));
	CHECK(!hdr_recording_profile_init(&p, true, 0, 0, 400, 400, 80));
	CHECK(!hdr_recording_profile_init(&p, true, 0, 0, 203, 1000.1, 80));
	CHECK(!hdr_recording_profile_init(&p, true, 0, 0, 79, 1000, 80));
	CHECK(!hdr_recording_profile_init(&p, true, 0, 0, 203, 1000, 49));
	CHECK(!hdr_recording_profile_init(NULL, true, 0, 0, 203, 1000, 80));
	CHECK(hdr_recording_profile_init(&p, false, 99, NAN, NAN, NAN, NAN));
	float unchanged[] = {-1.0f, 4.0f, 8000.0f}, original[3];
	memcpy(original, unchanged, sizeof original);
	hdr_recording_grade_nits(unchanged, &p);
	CHECK(memcmp(original, unchanged, sizeof original) == 0);
	q = p; q.exposure_ev = 4; CHECK(hdr_recording_profile_equal(&p, &q));

	unsigned long samples = 0;
	const double knees[] = {50, 80, 95};
	for (int mode = 0; mode <= 1; ++mode) {
		for (int peak_step = 0; peak_step <= 16; ++peak_step) {
			const double peak = 400.0 + 37.5 * peak_step;
			for (int k = 0; k < 3; ++k) {
				CHECK(hdr_recording_profile_init(&p, true, mode, 0, 203, peak, knees[k]));
				double previous = -1.0;
				for (int i = 0; i <= 10000; ++i) {
					const double in = (double)i * 10.0;
					const double out = hdr_recording_map_level(in, &p);
					CHECK(isfinite(out) && out >= previous && out <= peak && out <= in);
					previous = out;
					float rgb[] = {(float)in, (float)(in * .5), (float)(in * .25)};
					hdr_recording_grade_nits(rgb, &p);
					CHECK(rgb[0] <= peak && rgb[0] >= 0);
					CHECK(fabsf(rgb[1] - rgb[0] * .5f) < .001f);
					CHECK(fabsf(rgb[2] - rgb[0] * .25f) < .001f);
					++samples;
				}
			}
		}
	}
	CHECK(hdr_recording_profile_init(&p, true, 0, 0, 203, 1000, 80));
	const double knee = p.peak_nits * (double)p.knee_fraction;
	CHECK(fabs(hdr_recording_map_level(knee, &p) - knee) < 1e-9);
	CHECK(fabs((hdr_recording_map_level(knee + .001, &p) - knee) / .001 - 1) < 1e-5);
	CHECK(hdr_recording_map_level(DBL_MAX, &p) <= p.peak_nits);
	float black[] = {0, 0, 0}; hdr_recording_grade_nits(black, &p);
	CHECK(black[0] == 0 && black[1] == 0 && black[2] == 0);
	CHECK(hdr_recording_profile_init(&p, true, 0, 1, 203, 1000, 80));
	float exposure[] = {10, 20, 30}; hdr_recording_grade_nits(exposure, &p);
	CHECK(exposure[0] == 20 && exposure[1] == 40 && exposure[2] == 60);
	printf("PASS: %lu curve/color cases, profile bounds, mix-key isolation, knee continuity and bypass\n", samples);
	return 0;
}
