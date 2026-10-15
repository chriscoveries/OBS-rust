/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../../plugins/obs-filters/noise-gate-process.h"

#define CHANNELS 8
#define FRAMES 1032

static unsigned checks;
static unsigned failures;
static uint32_t rng = 0x912acc75;

static void check(bool condition, const char *name)
{
	++checks;
	if (!condition) {
		if (failures < 8)
			fprintf(stderr, "%s\n", name);
		++failures;
	}
}

/* Scalar oracle: the loop in noise_gate_filter_audio at base
 * 087a25c7d977ad3fbcc12a572d95ec05042824de, with audio->frames supplied
 * as a parameter. The order of every comparison and arithmetic operation
 * is retained. It deliberately does not call the production step helper. */
static void reference(struct noise_gate_state *ng, float **adata, size_t frames)
{
	const float close_threshold = ng->close_threshold;
	const float open_threshold = ng->open_threshold;
	const float sample_rate_i = ng->sample_rate_i;
	const float release_rate = ng->release_rate;
	const float attack_rate = ng->attack_rate;
	const float decay_rate = ng->decay_rate;
	const float hold_time = ng->hold_time;
	const size_t channels = ng->channels;

	for (size_t i = 0; i < frames; i++) {
		float cur_level = fabsf(adata[0][i]);
		for (size_t j = 0; j < channels; j++) {
			cur_level = fmaxf(cur_level, fabsf(adata[j][i]));
		}
		if (cur_level > open_threshold && !ng->is_open) {
			ng->is_open = true;
		}
		if (ng->level < close_threshold && ng->is_open) {
			ng->held_time = 0.0f;
			ng->is_open = false;
		}
		ng->level = fmaxf(ng->level, cur_level) - decay_rate;
		if (ng->is_open) {
			ng->attenuation = fminf(1.0f, ng->attenuation + attack_rate);
		} else {
			ng->held_time += sample_rate_i;
			if (ng->held_time > hold_time) {
				ng->attenuation = fmaxf(0.0f, ng->attenuation - release_rate);
			}
		}
		for (size_t c = 0; c < channels; c++)
			adata[c][i] *= ng->attenuation;
	}
}

static uint32_t next_u32(void)
{
	rng ^= rng << 13;
	rng ^= rng >> 17;
	rng ^= rng << 5;
	return rng;
}

static float from_bits(uint32_t bits)
{
	float f;
	memcpy(&f, &bits, sizeof(f));
	return f;
}

static bool same_float(float a, float b)
{
	return memcmp(&a, &b, sizeof(a)) == 0;
}

static bool same_state(const struct noise_gate_state *a, const struct noise_gate_state *b)
{
	return a->is_open == b->is_open && same_float(a->attenuation, b->attenuation) &&
	       same_float(a->level, b->level) && same_float(a->held_time, b->held_time);
}

static struct noise_gate_state initial(size_t channels, unsigned variant)
{
	const float rates[] = {8000.0f, 44100.0f, 48000.0f, 192000.0f};
	const float sr = rates[variant % 4];
	struct noise_gate_state s = {
		.sample_rate_i = 1.0f / sr,
		.channels = channels,
		.open_threshold = 0.4f,
		.close_threshold = 0.2f,
		.decay_rate = 0.2f / ((1.0f / 75.0f) * sr),
		.attack_rate = variant % 5 == 0 ? INFINITY : 1.0f / (0.025f * sr),
		.release_rate = variant % 7 == 0 ? INFINITY : 1.0f / (0.15f * sr),
		.hold_time = variant % 3 == 0 ? 0.0f : 0.005f,
		.is_open = variant % 2 != 0,
		.attenuation = variant % 2 != 0 ? 0.75f : 0.0f,
		.level = variant % 2 != 0 ? 0.5f : 0.0f,
		.held_time = 0.0f,
	};
	if (variant % 11 == 0) {
		s.close_threshold = 0.6f;
		s.decay_rate = -s.decay_rate;
	}
	return s;
}

static void parity_cases(void)
{
	float input[CHANNELS][FRAMES], actual[CHANNELS][FRAMES];
	float *a[CHANNELS], *b[CHANNELS];
	const size_t lengths[] = {0, 1, 2, 3, 4, 5, 7, 15, 31, 32, 33, 127, 128, 129, 1023, 1024, 1025};
	const uint32_t exceptional[] = {0, 0x80000000, 1, 0x80000001, 0x007fffff, 0x807fffff,
					0x7f800000, 0xff800000, 0x7fc12345, 0xffc56789};
	for (size_t channels = 1; channels <= CHANNELS; ++channels) {
		for (unsigned variant = 0; variant < 64; ++variant) {
			for (size_t l = 0; l < sizeof(lengths) / sizeof(lengths[0]); ++l) {
				const size_t frames = lengths[l];
				struct noise_gate_state ref = initial(channels, variant);
				struct noise_gate_state got = ref;
				for (size_t c = 0; c < channels; ++c) {
					for (size_t i = 0; i < FRAMES; ++i) {
						const uint32_t r = next_u32();
						input[c][i] = (float)(int32_t)(r % 65537u) / 16384.0f - 2.0f;
						if (variant % 4 == 0 && i % 17 == 0)
							input[c][i] = from_bits(exceptional[r % 10]);
						if (variant % 4 == 1)
							input[c][i] *= i % 53 < 20 ? 0.01f : 1.0f;
					}
					a[c] = input[c];
					b[c] = actual[c];
					memcpy(b[c], a[c], sizeof(input[c]));
				}
				/* Compare under each identical partition. State is checked
				 * at every boundary, not just at the end of the stream. */
				size_t at = 0;
				do {
					size_t count = frames - at;
					const size_t limit = variant % 3 == 0 ? 13 : (variant % 3 == 1 ? 128 : FRAMES);
					if (count > limit)
						count = limit;
					for (size_t c = 0; c < channels; ++c) {
						a[c] = input[c] + at;
						b[c] = actual[c] + at;
					}
					reference(&ref, a, count);
					noise_gate_process(&got, b, count);
					check(same_state(&ref, &got), "parity: carried state differs from original scalar loop");
					at += count;
				} while (at < frames);
				for (size_t c = 0; c < channels; ++c)
					check(memcmp(input[c], actual[c], sizeof(input[c])) == 0,
					      "parity: output or untouched tail differs from original scalar loop");
			}
		}
	}
}

static void alias_cases(void)
{
	float original[FRAMES], actual[FRAMES];
	for (size_t offset = 0; offset <= 4; ++offset) {
		for (unsigned reverse = 0; reverse < 2; ++reverse) {
			for (size_t i = 0; i < FRAMES; ++i)
				original[i] = (float)(i % 23) / 32.0f;
			memcpy(actual, original, sizeof(actual));
			float *ref_planes[] = {original + (reverse ? offset : 0), original + (reverse ? 0 : offset)};
			float *got_planes[] = {actual + (reverse ? offset : 0), actual + (reverse ? 0 : offset)};
			struct noise_gate_state ref = initial(2, 1), got = ref;
			reference(&ref, ref_planes, 1024);
			const size_t vector_frames = noise_gate_process(&got, got_planes, 1024);
			check(vector_frames == 0, "alias: overlapping planes must use scalar sample order");
			check(same_state(&ref, &got), "alias: carried state differs");
			check(memcmp(original, actual, sizeof(actual)) == 0, "alias: output differs");
		}
	}
}

static void transition_cases(void)
{
	float ref_data[CHANNELS][132], got_data[CHANNELS][132];
	float *a[CHANNELS], *b[CHANNELS];
	for (size_t channels = 1; channels <= CHANNELS; ++channels) {
		for (unsigned variant = 0; variant < 32; ++variant) {
			struct noise_gate_state ref = initial(channels, variant), got = ref;
			for (unsigned block = 0; block < 160; ++block) {
				for (size_t c = 0; c < channels; ++c) {
					for (size_t i = 0; i < 132; ++i) {
						/* Long quiet spans exercise close, hold and release.
						 * Exact threshold values exercise strict comparisons. */
						float sample = block % 40 < 3 ? 0.5f : 0.0f;
						if (block % 40 == 20)
							sample = i % 2 ? ref.close_threshold : ref.open_threshold;
						ref_data[c][i] = got_data[c][i] = c % 2 ? -sample : sample;
					}
					a[c] = ref_data[c];
					b[c] = got_data[c];
				}
				reference(&ref, a, 129);
				noise_gate_process(&got, b, 129);
				check(same_state(&ref, &got), "transitions: carried state differs");
				for (size_t c = 0; c < channels; ++c)
					check(memcmp(a[c], b[c], sizeof(ref_data[c])) == 0,
					      "transitions: samples or tail differ");
			}
		}
	}
	struct noise_gate_state state = initial(2, 0), before = state;
	check(noise_gate_process(&state, NULL, 0) == 0 && same_state(&state, &before),
	      "zero_frames: state changed or planes accessed");
}

#ifdef EXPECT_VECTOR_PATH
static void vector_path_runs(void)
{
#if defined(__SSE2__) || defined(_M_X64)
	float left[9] = {1, 1, 1, 1, 1, 1, 1, 1, 1};
	float right[9] = {1, 1, 1, 1, 1, 1, 1, 1, 1};
	float *planes[] = {left, right};
	struct noise_gate_state state = initial(2, 1);
	check(noise_gate_process(&state, planes, 9) == 8,
	      "sse2_processes_disjoint_full_vectors: expected 8 SIMD frames");
#endif
}
#endif

int main(void)
{
	parity_cases();
	alias_cases();
	transition_cases();
#ifdef EXPECT_VECTOR_PATH
	vector_path_runs();
#endif
	printf("noise_gate_process: checks=%u failures=%u\n", checks, failures);
	return failures ? 1 : 0;
}
