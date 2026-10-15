/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Complements the logarithm sweep with the two earlier arithmetic fixes. */
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "../../plugins/obs-filters/dynamics-simd.h"

#if DYN_SIMD_SSE2
static unsigned checked, failed;

static void check_gain(float actual, float expected)
{
	++checked;
	if (!isfinite(actual) || fabsf(actual - expected) > 1e-6f * fmaxf(1.0f, fabsf(expected))) {
		if (failed++ < 8)
			fprintf(stderr, "gain got=%a expected=%a\n", (double)actual, (double)expected);
	}
}

static void negative_exponents(void)
{
	const float input[4] = {-0.125f, -0.25f, -0.5f, -1.0f};
	float actual[4];
	_mm_storeu_ps(actual, dyn_pow10f_ps(_mm_loadu_ps(input)));
	for (size_t i = 0; i < 4; ++i)
		check_gain(actual[i], powf(10.0f, input[i]));
}

static void gain_and_tails(void)
{
	const float slopes[] = {0.0f, -0.0f, 1.0f};
	const float gains[] = {0.0f, 0.75f, 1.0f};
	for (size_t s = 0; s < 3; ++s) {
		for (size_t g = 0; g < 3; ++g) {
			for (uint32_t count = 0; count <= 17; ++count) {
				float env[18], a[18], b[18], expected[18];
				float *channels[] = {a, NULL, b};
				for (size_t i = 0; i < 18; ++i) {
					env[i] = i % 3 ? 1.0f : 0.0f;
					a[i] = b[i] = (i & 1) ? -0.5f : 0.25f;
					expected[i] = a[i];
					if (i < count) {
						const float db = fminf(0.0f, slopes[s] * (-20.0f - mul_to_db(env[i])));
						expected[i] *= db_to_mul(db) * gains[g];
					}
				}
				dyn_compression_gain_apply(env, channels, 3, count, slopes[s], -20.0f, gains[g]);
				for (size_t i = 0; i < 18; ++i) {
					check_gain(a[i], expected[i]);
					check_gain(b[i], expected[i]);
				}
			}
		}
	}
}
#endif

int main(void)
{
#if DYN_SIMD_SSE2
	negative_exponents();
	gain_and_tails();
	printf("combined gain checks=%u failures=%u\n", checked, failed);
	return failed ? 1 : 0;
#else
	puts("SKIP: SSE2 is required");
	return 77;
#endif
}
