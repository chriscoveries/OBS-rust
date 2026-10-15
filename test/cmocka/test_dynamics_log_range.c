/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Standalone CTest executable: the actual production header, no libobs process. */
#include <float.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "../../plugins/obs-filters/dynamics-simd.h"

#if DYN_SIMD_SSE2
static unsigned failures;
static unsigned cases;
static double max_scaled_error;

static void check_four(const float input[4])
{
	float actual[4];
	_mm_storeu_ps(actual, dyn_logf_ps(_mm_loadu_ps(input)));
	for (size_t lane = 0; lane < 4; ++lane) {
		const double expected = log((double)input[lane]);
		const double scaled = fabs((double)actual[lane] - expected) / (1.0 + fabs(expected));
		++cases;
		if (scaled > max_scaled_error)
			max_scaled_error = scaled;
		/* Absolute-plus-relative bound remains meaningful at log(1) == 0. */
		if (!isfinite(actual[lane]) || scaled > 1e-6) {
			if (failures++ < 4)
				fprintf(stderr, "log input=%a got=%.10g expected=%.10g\n", (double)input[lane],
					(double)actual[lane], expected);
		}
	}
}

static void gain_boundary(void)
{
	float env[5] = {nextafterf(1.0f, 0.0f), 0.99f, 0.75f, 1.99f, 0.99f};
	float samples[5] = {1, 1, 1, 1, 1};
	float *channels[1] = {samples};
	dyn_compression_gain_apply(env, channels, 1, 5, 0.75f, -12.0f, 1.0f);
	for (size_t i = 0; i < 5; ++i) {
		const float expected = db_to_mul(fminf(0.0f, 0.75f * (-12.0f - mul_to_db(env[i]))));
		if (!isfinite(samples[i]) || fabsf(samples[i] - expected) > 1e-5f * expected) {
			++failures;
			fprintf(stderr, "gain lane=%zu got=%.10g expected=%.10g\n", i, (double)samples[i],
				(double)expected);
		}
	}
}
#endif

int main(void)
{
#if DYN_SIMD_SSE2
	/* 254 normal exponents, 4096 deterministic mantissas each. All lanes vary. */
	for (uint32_t exponent = 1; exponent <= 254; ++exponent) {
		for (uint32_t mantissa = 0; mantissa < 4096; mantissa += 4) {
			float input[4];
			for (uint32_t lane = 0; lane < 4; ++lane) {
				const uint32_t bits = (exponent << 23) | ((mantissa + lane) * 2048u);
				memcpy(&input[lane], &bits, sizeof(bits));
			}
			check_four(input);
		}
	}
	const float near_one[4] = {nextafterf(1.0f, 0.0f), 1.0f, nextafterf(1.0f, INFINITY),
				   nextafterf(2.0f, 0.0f)};
	const float extremes[4] = {FLT_MIN, FLT_MAX, 1e-30f, 0.707106781186547524f};
	check_four(near_one);
	check_four(extremes);
	gain_boundary();
	printf("log cases=%u failures=%u max_scaled_error=%.10g\n", cases, failures, max_scaled_error);
	return failures ? 1 : 0;
#else
	puts("SKIP: requires the x86 SSE2 implementation; native arm64 is not coverage");
	return 77;
#endif
}
