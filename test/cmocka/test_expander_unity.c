/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Exercise the production scalar filter, including its carried gain state. */
#include <stdio.h>
#include <string.h>

#include "../../plugins/obs-filters/expander-filter.c"

const char *obs_module_text(const char *text)
{
	return text;
}

static unsigned checks;
static unsigned failures;

static void check(bool passed, const char *test, const char *message)
{
	++checks;
	if (!passed) {
		if (failures < 8)
			fprintf(stderr, "%s: %s\n", test, message);
		++failures;
	}
}

static bool same_float(float a, float b)
{
	return memcmp(&a, &b, sizeof(a)) == 0;
}

static struct expander_data *create_filter(size_t channels, size_t sample_rate, float ratio, int detector,
					   float output_gain)
{
	struct expander_data *cd = bzalloc(sizeof(*cd));
	cd->num_channels = channels;
	cd->sample_rate = sample_rate;
	cd->ratio = ratio;
	cd->slope = 1.0f - ratio;
	cd->threshold = -40.0f;
	cd->attack_gain = gain_coefficient((uint32_t)sample_rate, 0.010f);
	cd->release_gain = gain_coefficient((uint32_t)sample_rate, 0.050f);
	cd->output_gain = output_gain;
	cd->detector = detector;
	return cd;
}

static void ratio_one_silence_keeps_zero_gain(void)
{
	const char *test = "ratio_one_silence_keeps_zero_gain";
	float samples[1] = {0.0f};
	float envelope[1] = {0.0f};
	float gain[1] = {0.0f};
	process_sample(0, samples, envelope, gain, false, 0.0f, -40.0f, 0.0f, 0.9f, 0.1f, 0.99f, 0.01f, 1.0f, 0.0f);
	check(gain[0] == 0.0f, test, "silence changed gain at ratio 1:1");
}

static void ratio_one_silence_then_signal_is_passthrough(void)
{
	const char *test = "ratio_one_silence_then_signal_is_passthrough";
	const size_t rates[] = {8000, 44100, 48000, 96000, 192000};
	const size_t channel_counts[] = {1, 2, 6, 8};
	const float gains[] = {1.0f, 0.5f};
	const uint32_t lengths[] = {1, 3, 32, 1024};

	for (size_t rate = 0; rate < sizeof(rates) / sizeof(rates[0]); ++rate) {
		for (size_t count = 0; count < sizeof(channel_counts) / sizeof(channel_counts[0]); ++count) {
			for (int detector = RMS_DETECT; detector <= PEAK_DETECT; detector += 2) {
				for (size_t g = 0; g < sizeof(gains) / sizeof(gains[0]); ++g) {
					struct expander_data *cd = create_filter(channel_counts[count], rates[rate],
										 1.0f, detector, gains[g]);
					float data[MAX_AUDIO_CHANNELS][1024] = {{0}};
					struct obs_audio_data audio = {0};
					audio.frames = 1024;
					for (size_t c = 0; c < cd->num_channels; ++c)
						audio.data[c] = (uint8_t *)data[c];

					/* Carry state across blocks, as the real audio callback does. */
					for (unsigned block = 0; block < 8; ++block)
						expander_filter_audio(cd, &audio);
					for (size_t c = 0; c < cd->num_channels; ++c)
						check(cd->gain_db_buf[c] == 0.0f, test,
						      "silent blocks accumulated attenuation");

					for (size_t block = 0; block < sizeof(lengths) / sizeof(lengths[0]); ++block) {
						audio.frames = lengths[block];
						for (size_t c = 0; c < cd->num_channels; ++c) {
							for (uint32_t i = 0; i < audio.frames; ++i)
								data[c][i] = ((i + c) & 1u) ? -0.25f : 0.25f;
						}
						expander_filter_audio(cd, &audio);
						for (size_t c = 0; c < cd->num_channels; ++c) {
							for (uint32_t i = 0; i < audio.frames; ++i) {
								const float input = ((i + c) & 1u) ? -0.25f : 0.25f;
								check(same_float(data[c][i], input * gains[g]), test,
								      "signal after silence differs from input times output gain");
							}
						}
					}
					expander_destroy(cd);
				}
			}
		}
	}
}

static void ratio_one_preserves_existing_gain_history(void)
{
	const char *test = "ratio_one_preserves_existing_gain_history";
	float samples[1] = {0.0f};
	float envelope[1] = {0.0f};
	float gain[1] = {0.0f};
	process_sample(0, samples, envelope, gain, false, -12.0f, -40.0f, 0.0f, 0.5f, 0.5f, 0.75f, 0.25f, 1.0f, 0.0f);
	check(same_float(gain[0], -6.0f), test, "existing attenuation did not release toward unity via attack");
}

static void nonunity_and_upward_paths_are_preserved(void)
{
	const char *test = "nonunity_and_upward_paths_are_preserved";
	float samples[1] = {0.0f};
	float envelope[1] = {0.0f};
	float gain[1] = {0.0f};
	process_sample(0, samples, envelope, gain, false, 0.0f, -40.0f, -1.0f, 0.5f, 0.5f, 0.75f, 0.25f, 1.0f, 0.0f);
	check(same_float(gain[0], -15.0f), test, "ratio 2:1 lost the ordinary -60 dB silence target");
	process_sample(0, samples, envelope, gain, true, 0.0f, -20.0f, 0.5f, 0.5f, 0.5f, 0.75f, 0.25f, 1.0f, 10.0f);
	check(gain[0] == 0.0f, test, "upward-compressor silence changed");
}

int main(void)
{
	ratio_one_silence_keeps_zero_gain();
	ratio_one_silence_then_signal_is_passthrough();
	ratio_one_preserves_existing_gain_history();
	nonunity_and_upward_paths_are_preserved();
	printf("expander unity: checks=%u failures=%u\n", checks, failures);
	return failures ? 1 : 0;
}
