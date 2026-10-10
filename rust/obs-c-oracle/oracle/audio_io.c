/* Test-only oracle for libobs/media-io/audio-io.c: the real C file compiled
 * with its exported and static entry points renamed oracle_* so the ported
 * Rust unit can be diffed against it. Also supplies, for cargo-test links
 * only, the libobs symbols audio-io.c calls that no oracle source provides:
 * the profiler hooks (no-ops), obs_get_profiler_name_store (NULL),
 * audio_resampler_* (create fails NULL like an absent backend), and the two
 * platform-nix.c time helpers (real POSIX implementations, same semantics).
 * pthread/os_event/os_set_thread_name come from the real threading-posix.c
 * and blog from the real base-variadic.c already compiled here. */
#define _GNU_SOURCE
#include <stddef.h>
#include <stdint.h>
#include <time.h>
#include <unistd.h>
#include <string.h>
#include <stdarg.h>
#include <stdio.h>

#define audio_output_open oracle_audio_output_open
#define audio_output_close oracle_audio_output_close
#define audio_output_connect oracle_audio_output_connect
#define audio_output_disconnect oracle_audio_output_disconnect
#define audio_output_active oracle_audio_output_active
#define audio_output_get_block_size oracle_audio_output_get_block_size
#define audio_output_get_planes oracle_audio_output_get_planes
#define audio_output_get_channels oracle_audio_output_get_channels
#define audio_output_get_sample_rate oracle_audio_output_get_sample_rate
#define audio_output_get_info oracle_audio_output_get_info
#define input_and_output oracle_input_and_output
#define audio_thread oracle_audio_thread
#define audio_input_init oracle_audio_input_init
#define audio_get_input_idx oracle_audio_get_input_idx
#define do_audio_output oracle_do_audio_output
#define clamp_audio_output oracle_clamp_audio_output
#define resample_audio_output oracle_resample_audio_output
#define audio_input_free oracle_audio_input_free
#define valid_audio_params oracle_valid_audio_params

#include "media-io/audio-io.c"

/* ------------------------------------------------------------------ */
/* stubs/impls for libobs symbols absent from the oracle link          */

uint64_t os_gettime_ns(void)
{
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return ((uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec);
}

bool os_sleepto_ns_fast(uint64_t time_target)
{
	uint64_t current = os_gettime_ns();
	if (time_target < current)
		return false;

	do {
		uint64_t remain_us = (time_target - current + 999) / 1000;
		unsigned long us = remain_us >= 1000000 ? 999999 : remain_us;
		struct timespec ts = {us / 1000000, (us % 1000000) * 1000};
		nanosleep(&ts, NULL);
		current = os_gettime_ns();
	} while (time_target > current);

	return true;
}

profiler_name_store_t *obs_get_profiler_name_store(void)
{
	return NULL;
}

const char *profile_store_name(profiler_name_store_t *store, const char *format, ...)
{
	(void)store;
	(void)format;
	return NULL;
}

void profile_start(const char *name)
{
	(void)name;
}
void profile_end(const char *name)
{
	(void)name;
}
void profile_reenable_thread(void) {}

audio_resampler_t *audio_resampler_create(const struct resample_info *dst, const struct resample_info *src)
{
	(void)dst;
	(void)src;
	return NULL;
}

void audio_resampler_destroy(audio_resampler_t *resampler)
{
	(void)resampler;
}

bool audio_resampler_resample(audio_resampler_t *resampler, uint8_t *output[], uint32_t *out_frames,
			      uint64_t *ts_offset, const uint8_t *const input[], uint32_t in_frames)
{
	(void)resampler;
	(void)output;
	(void)out_frames;
	(void)ts_offset;
	(void)input;
	(void)in_frames;
	return false;
}

/* ------------------------------------------------------------------ */
/* oracle-only extras: drive one tick + expose internal state          */

void oracle_audio_tick(audio_t *audio, uint64_t audio_time, uint64_t prev_time)
{
	input_and_output(audio, audio_time, prev_time);
}

size_t oracle_audio_mix_input_count(audio_t *audio, size_t mix_idx)
{
	return audio->mixes[mix_idx].inputs.num;
}

bool oracle_audio_mix_input_resampled(audio_t *audio, size_t mix_idx, size_t input_idx)
{
	return audio->mixes[mix_idx].inputs.array[input_idx].resampler != NULL;
}

const struct audio_convert_info *oracle_audio_mix_input_conversion(audio_t *audio, size_t mix_idx,
								   size_t input_idx)
{
	return &audio->mixes[mix_idx].inputs.array[input_idx].conversion;
}

/* The layout the C compiler gives the structs the API crosses. */
size_t oracle_audio_output_info_size(void)
{
	return sizeof(struct audio_output_info);
}
size_t oracle_audio_output_info_align(void)
{
	return _Alignof(struct audio_output_info);
}
size_t oracle_audio_output_info_offset_name(void)
{
	return offsetof(struct audio_output_info, name);
}
size_t oracle_audio_output_info_offset_samples_per_sec(void)
{
	return offsetof(struct audio_output_info, samples_per_sec);
}
size_t oracle_audio_output_info_offset_format(void)
{
	return offsetof(struct audio_output_info, format);
}
size_t oracle_audio_output_info_offset_speakers(void)
{
	return offsetof(struct audio_output_info, speakers);
}
size_t oracle_audio_output_info_offset_input_callback(void)
{
	return offsetof(struct audio_output_info, input_callback);
}
size_t oracle_audio_output_info_offset_input_param(void)
{
	return offsetof(struct audio_output_info, input_param);
}

size_t oracle_audio_convert_info_size(void)
{
	return sizeof(struct audio_convert_info);
}
size_t oracle_audio_convert_info_align(void)
{
	return _Alignof(struct audio_convert_info);
}
size_t oracle_audio_convert_info_offset_samples_per_sec(void)
{
	return offsetof(struct audio_convert_info, samples_per_sec);
}
size_t oracle_audio_convert_info_offset_format(void)
{
	return offsetof(struct audio_convert_info, format);
}
size_t oracle_audio_convert_info_offset_speakers(void)
{
	return offsetof(struct audio_convert_info, speakers);
}
size_t oracle_audio_convert_info_offset_allow_clipping(void)
{
	return offsetof(struct audio_convert_info, allow_clipping);
}

size_t oracle_audio_data_size(void)
{
	return sizeof(struct audio_data);
}
size_t oracle_audio_data_align(void)
{
	return _Alignof(struct audio_data);
}
size_t oracle_audio_data_offset_data(void)
{
	return offsetof(struct audio_data, data);
}
size_t oracle_audio_data_offset_frames(void)
{
	return offsetof(struct audio_data, frames);
}
size_t oracle_audio_data_offset_timestamp(void)
{
	return offsetof(struct audio_data, timestamp);
}

size_t oracle_audio_output_data_size(void)
{
	return sizeof(struct audio_output_data);
}
size_t oracle_audio_output_data_align(void)
{
	return _Alignof(struct audio_output_data);
}
size_t oracle_audio_output_data_offset_data(void)
{
	return offsetof(struct audio_output_data, data);
}
