/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Compile the exact effect function as C++ arithmetic. This catches reference
 * drift; it does not replace compilation/execution by an OBS graphics backend. */
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include "../../../libobs/hdr-recording-profile.h"
struct float3 {
	float r, g, b;
	float3(float r_, float g_, float b_) : r(r_), g(g_), b(b_) {}
};
static float3 operator*(float3 a, float v) { return {a.r * v, a.g * v, a.b * v}; }
static float3 max(float3 a, float3 b) { return {std::fmax(a.r, b.r), std::fmax(a.g, b.g), std::fmax(a.b, b.b)}; }
static float max(float a, float b) { return std::fmax(a, b); }
static float min(float a, float b) { return std::fmin(a, b); }
#define uniform
#include "../../../libobs/data/hdr-recording.effect"
#undef uniform
static void check(bool ok) { if (!ok) { std::fprintf(stderr, "shader arithmetic mismatch\n"); std::exit(1); } }
int main()
{
	uint32_t rng = 17;
	size_t count = 0;
	struct hdr_recording_profile p;
	for (int mode = 0; mode < 2; ++mode) {
		for (int ev = -4; ev <= 4; ++ev) {
			check(hdr_recording_profile_init(&p, true, mode, ev, 203, 1000, 80));
			hdr_recording_enabled = 1.0f;
			hdr_recording_exposure = std::exp2(float(ev));
			hdr_recording_peak = p.peak_nits;
			hdr_recording_knee = p.knee_fraction;
			hdr_recording_soft = mode == HDR_RECORDING_SOFT ? 1.0f : 0.0f;
			for (int n = 0; n < 20000; ++n) {
				float rgb[3];
				for (int c = 0; c < 3; ++c) {
					rng = rng * 1664525u + 1013904223u;
					rgb[c] = float(int(rng >> 8) - 10000) / 1200.0f;
				}
				const float3 actual = hdr_recording_grade({rgb[0] / 10000.0f, rgb[1] / 10000.0f, rgb[2] / 10000.0f});
				hdr_recording_grade_nits(rgb, &p);
				const float values[3] = {actual.r * 10000.0f, actual.g * 10000.0f, actual.b * 10000.0f};
				for (int c = 0; c < 3; ++c) {
					check(std::isfinite(values[c]));
					check(std::fabs(values[c] - rgb[c]) <= 0.001f + 0.000002f * std::fabs(rgb[c]));
				}
				++count;
			}
		}
	}
	/* Reset uniforms between HDR and disabled mixes; bypass is exact. */
	hdr_recording_enabled = 0.0f;
	const float3 unchanged = hdr_recording_grade({-1.0f, 0.1f, 2.0f});
	check(unchanged.r == -1.0f && unchanged.g == 0.1f && unchanged.b == 2.0f);
	std::printf("PASS: %zu exact-effect/reference comparisons and disabled bypass\n", count);
}
