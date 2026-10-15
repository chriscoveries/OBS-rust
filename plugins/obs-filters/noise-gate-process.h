/* SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once

#include <math.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

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

/* Returns the number of samples handled by the vector path. The production
 * callback ignores this private diagnostic value, so an inlined caller can
 * eliminate the counter. No public API or runtime setting is added. */
static inline size_t noise_gate_process(struct noise_gate_state *ng, float **planes, size_t frames)
{
	for (size_t i = 0; i < frames; ++i)
		noise_gate_scalar_sample(ng, planes, i);
	return 0;
}
