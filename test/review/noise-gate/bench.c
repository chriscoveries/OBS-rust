/* SPDX-License-Identifier: GPL-2.0-or-later */
#define _POSIX_C_SOURCE 200809L
#define main noise_gate_regression_main
#include "../../cmocka/test_noise_gate_process.c"
#undef main
#include <inttypes.h>
#include <time.h>

#define BATCH 64
#define BLOCK 1024

static uint64_t monotonic_ns(void)
{
	struct timespec ts;
	if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0)
		exit(2);
	return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
}

static void process_batch(bool vector, struct noise_gate_state *state, float *data, size_t channels)
{
	for (size_t block = 0; block < BATCH; ++block) {
		float *planes[CHANNELS];
		for (size_t c = 0; c < channels; ++c)
			planes[c] = data + (block * channels + c) * BLOCK;
		if (vector)
			noise_gate_process(state, planes, BLOCK);
		else
			reference(state, planes, BLOCK);
	}
}

static uint64_t measure(bool vector, const float *seed, float *data, size_t channels, size_t batches,
			float *checksum)
{
	const size_t bytes = channels * BATCH * BLOCK * sizeof(float);
	struct noise_gate_state state = initial(channels, 1);
	state.sample_rate_i = 1.0f / 48000.0f;
	state.decay_rate = 0.2f / ((1.0f / 75.0f) * 48000.0f);
	state.attack_rate = 1.0f / (0.025f * 48000.0f);
	state.release_rate = 1.0f / (0.15f * 48000.0f);
	state.hold_time = 0.2f;
	uint64_t total = 0;
	*checksum = 0.0f;
	for (size_t b = 0; b < batches; ++b) {
		memcpy(data, seed, bytes);
		const uint64_t start = monotonic_ns();
		process_batch(vector, &state, data, channels);
		total += monotonic_ns() - start;
		/* Consume results outside the measured interval. */
		*checksum += data[(b % BATCH) * channels * BLOCK] + state.attenuation + state.held_time;
	}
	return total;
}

int main(int argc, char **argv)
{
	if (argc != 3 || (strcmp(argv[1], "2") != 0 && strcmp(argv[1], "6") != 0) ||
	    (strcmp(argv[2], "AB") != 0 && strcmp(argv[2], "BA") != 0))
		return 2;
#if !NOISE_GATE_HAS_SSE2
	return 77;
#else
	const size_t channels = (size_t)atoi(argv[1]);
	const size_t elements = channels * BATCH * BLOCK;
	const size_t bytes = elements * sizeof(float);
	float *seed = malloc(bytes), *before = malloc(bytes), *after = malloc(bytes);
	if (!seed || !before || !after)
		return 2;
	for (size_t i = 0; i < elements; ++i) {
		const size_t block = i / (channels * BLOCK);
		const float sample = (float)(int32_t)(next_u32() % 65537) / 32768.0f - 1.0f;
		seed[i] = sample * (block % 40 < 15 ? 0.75f : 0.01f);
	}
	memcpy(before, seed, bytes);
	memcpy(after, seed, bytes);
	struct noise_gate_state ref = initial(channels, 1), got = ref;
	process_batch(false, &ref, before, channels);
	process_batch(true, &got, after, channels);
	if (memcmp(before, after, bytes) != 0 || !same_state(&ref, &got)) {
		fputs("pre_timing_parity: output or state differs\n", stderr);
		return 1;
	}
	float warm, a, b;
	measure(false, seed, before, channels, 16, &warm);
	measure(true, seed, after, channels, 16, &warm);
	uint64_t old_ns, new_ns;
	if (argv[2][0] == 'A') {
		old_ns = measure(false, seed, before, channels, 128, &a);
		new_ns = measure(true, seed, after, channels, 128, &b);
	} else {
		new_ns = measure(true, seed, after, channels, 128, &b);
		old_ns = measure(false, seed, before, channels, 128, &a);
	}
	const bool equal = same_float(a, b) && memcmp(before, after, bytes) == 0;
	printf("{\"case\":\"noise-gate-%zuch\",\"before_ns\":%" PRIu64 ",\"after_ns\":%" PRIu64
	       ",\"frames\":8192,\"unit\":\"1024-sample block\",\"order\":\"%s\",\"output_equal\":%s}\n",
	       channels, old_ns, new_ns, argv[2], equal ? "true" : "false");
	free(seed);
	free(before);
	free(after);
	return equal ? 0 : 1;
#endif
}
