#include <media-io/audio-math.h>
#include <obs-module.h>
#include <math.h>

#include "noise-gate-process.h"

#define do_log(level, format, ...) \
	blog(level, "[noise gate: '%s'] " format, obs_source_get_name(ng->context), ##__VA_ARGS__)

#define warn(format, ...) do_log(LOG_WARNING, format, ##__VA_ARGS__)
#define info(format, ...) do_log(LOG_INFO, format, ##__VA_ARGS__)

/* clang-format off */

#define S_OPEN_THRESHOLD               "open_threshold"
#define S_CLOSE_THRESHOLD              "close_threshold"
#define S_ATTACK_TIME                  "attack_time"
#define S_HOLD_TIME                    "hold_time"
#define S_RELEASE_TIME                 "release_time"

#define MT_ obs_module_text
#define TEXT_OPEN_THRESHOLD            MT_("NoiseGate.OpenThreshold")
#define TEXT_CLOSE_THRESHOLD           MT_("NoiseGate.CloseThreshold")
#define TEXT_ATTACK_TIME               MT_("NoiseGate.AttackTime")
#define TEXT_HOLD_TIME                 MT_("NoiseGate.HoldTime")
#define TEXT_RELEASE_TIME              MT_("NoiseGate.ReleaseTime")

/* clang-format on */

struct noise_gate_data {
	obs_source_t *context;
	struct noise_gate_state state;
};

#define VOL_MIN -96.0
#define VOL_MAX 0.0

static const char *noise_gate_name(void *unused)
{
	UNUSED_PARAMETER(unused);
	return obs_module_text("NoiseGate");
}

static void noise_gate_destroy(void *data)
{
	struct noise_gate_data *ng = data;
	bfree(ng);
}

static inline float ms_to_secf(int ms)
{
	return (float)ms / 1000.0f;
}

static void noise_gate_update(void *data, obs_data_t *s)
{
	struct noise_gate_data *ng = data;
	float open_threshold_db;
	float close_threshold_db;
	float sample_rate;
	int attack_time_ms;
	int hold_time_ms;
	int release_time_ms;

	open_threshold_db = (float)obs_data_get_double(s, S_OPEN_THRESHOLD);
	close_threshold_db = (float)obs_data_get_double(s, S_CLOSE_THRESHOLD);
	attack_time_ms = (int)obs_data_get_int(s, S_ATTACK_TIME);
	hold_time_ms = (int)obs_data_get_int(s, S_HOLD_TIME);
	release_time_ms = (int)obs_data_get_int(s, S_RELEASE_TIME);
	sample_rate = (float)audio_output_get_sample_rate(obs_get_audio());

	ng->state.sample_rate_i = 1.0f / sample_rate;
	ng->state.channels = audio_output_get_channels(obs_get_audio());
	ng->state.open_threshold = db_to_mul(open_threshold_db);
	ng->state.close_threshold = db_to_mul(close_threshold_db);
	ng->state.attack_rate = 1.0f / (ms_to_secf(attack_time_ms) * sample_rate);
	ng->state.release_rate = 1.0f / (ms_to_secf(release_time_ms) * sample_rate);

	const float threshold_diff = ng->state.open_threshold - ng->state.close_threshold;
	const float min_decay_period = (1.0f / 75.0f) * sample_rate;

	ng->state.decay_rate = threshold_diff / min_decay_period;
	ng->state.hold_time = ms_to_secf(hold_time_ms);
	ng->state.is_open = false;
	ng->state.attenuation = 0.0f;
	ng->state.level = 0.0f;
	ng->state.held_time = 0.0f;
}

static void *noise_gate_create(obs_data_t *settings, obs_source_t *filter)
{
	struct noise_gate_data *ng = bzalloc(sizeof(*ng));
	ng->context = filter;
	noise_gate_update(ng, settings);
	return ng;
}

static struct obs_audio_data *noise_gate_filter_audio(void *data, struct obs_audio_data *audio)
{
	struct noise_gate_data *ng = data;
	noise_gate_process(&ng->state, (float **)audio->data, audio->frames);
	return audio;
}

static void noise_gate_defaults(obs_data_t *s)
{
	obs_data_set_default_double(s, S_OPEN_THRESHOLD, -26.0);
	obs_data_set_default_double(s, S_CLOSE_THRESHOLD, -32.0);
	obs_data_set_default_int(s, S_ATTACK_TIME, 25);
	obs_data_set_default_int(s, S_HOLD_TIME, 200);
	obs_data_set_default_int(s, S_RELEASE_TIME, 150);
}

static obs_properties_t *noise_gate_properties(void *data)
{
	obs_properties_t *ppts = obs_properties_create();
	obs_property_t *p;

	p = obs_properties_add_float_slider(ppts, S_CLOSE_THRESHOLD, TEXT_CLOSE_THRESHOLD, VOL_MIN, VOL_MAX, 1.0);
	obs_property_float_set_suffix(p, " dB");
	p = obs_properties_add_float_slider(ppts, S_OPEN_THRESHOLD, TEXT_OPEN_THRESHOLD, VOL_MIN, VOL_MAX, 1.0);
	obs_property_float_set_suffix(p, " dB");
	p = obs_properties_add_int(ppts, S_ATTACK_TIME, TEXT_ATTACK_TIME, 0, 10000, 1);
	obs_property_int_set_suffix(p, " ms");
	p = obs_properties_add_int(ppts, S_HOLD_TIME, TEXT_HOLD_TIME, 0, 10000, 1);
	obs_property_int_set_suffix(p, " ms");
	p = obs_properties_add_int(ppts, S_RELEASE_TIME, TEXT_RELEASE_TIME, 0, 10000, 1);
	obs_property_int_set_suffix(p, " ms");

	UNUSED_PARAMETER(data);
	return ppts;
}

struct obs_source_info noise_gate_filter = {
	.id = "noise_gate_filter",
	.type = OBS_SOURCE_TYPE_FILTER,
	.output_flags = OBS_SOURCE_AUDIO,
	.get_name = noise_gate_name,
	.create = noise_gate_create,
	.destroy = noise_gate_destroy,
	.update = noise_gate_update,
	.filter_audio = noise_gate_filter_audio,
	.get_defaults = noise_gate_defaults,
	.get_properties = noise_gate_properties,
};
