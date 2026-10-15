/* SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once

#include <math.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#if defined(__SSE2__) || defined(_M_X64) || (defined(_M_IX86_FP) && _M_IX86_FP >= 2)
#define NOISE_GATE_HAS_SSE2 1
#include <emmintrin.h>
#else
#define NOISE_GATE_HAS_SSE2 0
#endif

/* Private filter state. One instance is carried across every audio block. */
struct noise_gate_state {
	float sample_rate_i;
	size_t channels;
	float open_threshold;
	float close_threshold;
	float decay_rate;
	float attack_rate;
	float release_rate;
	float hold_time;
	bool is_open;
	float attenuation;
	float level;
	float held_time;
};

static inline void noise_gate_step(struct noise_gate_state *ng, float cur_level)
{
	if (cur_level > ng->open_threshold && !ng->is_open)
		ng->is_open = true;
	if (ng->level < ng->close_threshold && ng->is_open) {
		ng->held_time = 0.0f;
		ng->is_open = false;
	}
	ng->level = fmaxf(ng->level, cur_level) - ng->decay_rate;
	if (ng->is_open) {
		ng->attenuation = fminf(1.0f, ng->attenuation + ng->attack_rate);
	} else {
		ng->held_time += ng->sample_rate_i;
		if (ng->held_time > ng->hold_time)
			ng->attenuation = fmaxf(0.0f, ng->attenuation - ng->release_rate);
	}
}

static inline void noise_gate_scalar_sample(struct noise_gate_state *ng, float **planes, size_t i)
{
	float cur_level = fabsf(planes[0][i]);
	for (size_t c = 0; c < ng->channels; ++c)
		cur_level = fmaxf(cur_level, fabsf(planes[c][i]));
	noise_gate_step(ng, cur_level);
	for (size_t c = 0; c < ng->channels; ++c)
		planes[c][i] *= ng->attenuation;
}

#if NOISE_GATE_HAS_SSE2
/* Batching reads ahead by four samples. Preserve the legacy sample order
 * if a plugin supplies repeated or partially overlapping channel planes. */
static inline bool noise_gate_planes_disjoint(float **planes, size_t channels, size_t frames)
{
	if (frames > UINTPTR_MAX / sizeof(float))
		return false;
	const uintptr_t bytes = frames * sizeof(float);
	for (size_t c = 0; c < channels; ++c) {
		const uintptr_t begin = (uintptr_t)planes[c];
		if (begin > UINTPTR_MAX - bytes)
			return false;
		for (size_t previous = 0; previous < c; ++previous) {
			const uintptr_t other = (uintptr_t)planes[previous];
			if (begin < other + bytes && other < begin + bytes)
				return false;
		}
	}
	return true;
}

static inline bool noise_gate_levels4(float **planes, size_t channels, size_t offset, float out[4])
{
	const __m128 sign = _mm_set1_ps(-0.0f);
	const __m128i infinity = _mm_set1_epi32(0x7f800000);
	__m128 levels = _mm_setzero_ps();
	for (size_t c = 0; c < channels; ++c) {
		const __m128 samples = _mm_andnot_ps(sign, _mm_loadu_ps(planes[c] + offset));
		/* Integer detection avoids doing floating-point max on NaNs.
		 * The original scalar path handles those four samples instead. */
		const __m128i nan = _mm_cmpgt_epi32(_mm_castps_si128(samples), infinity);
		if (_mm_movemask_ps(_mm_castsi128_ps(nan)))
			return false;
		levels = _mm_max_ps(levels, samples);
	}
	_mm_storeu_ps(out, levels);
	return true;
}
#endif

/* Returns the number of samples handled by the vector path. The production
 * callback ignores this private diagnostic value, so an inlined caller can
 * eliminate the counter. No public API or runtime setting is added. */
static inline size_t noise_gate_process(struct noise_gate_state *ng, float **planes, size_t frames)
{
	size_t i = 0;
	size_t vector_frames = 0;
#if NOISE_GATE_HAS_SSE2
	if (ng->channels > 1 && frames >= 4 && noise_gate_planes_disjoint(planes, ng->channels, frames)) {
		for (; frames - i >= 4; i += 4) {
			float levels[4], gains[4];
			if (!noise_gate_levels4(planes, ng->channels, i, levels)) {
				for (size_t k = 0; k < 4; ++k)
					noise_gate_scalar_sample(ng, planes, i + k);
				continue;
			}
			/* The state machine remains sequential and uses the original
			 * operation order, including its previous-level close test. */
			for (size_t k = 0; k < 4; ++k) {
				noise_gate_step(ng, levels[k]);
				gains[k] = ng->attenuation;
			}
			const __m128 gain = _mm_loadu_ps(gains);
			for (size_t c = 0; c < ng->channels; ++c) {
				float *dst = planes[c] + i;
				_mm_storeu_ps(dst, _mm_mul_ps(_mm_loadu_ps(dst), gain));
			}
			vector_frames += 4;
		}
	}
#endif
	for (; i < frames; ++i)
		noise_gate_scalar_sample(ng, planes, i);
	return vector_frames;
}
