#include "test_mac_captions_api.h"

#include <stdlib.h>

struct mac_captions_core {
	bool enabled;
};

struct mac_captions_core *mac_captions_core_create(const struct mac_captions_transcriber_ops *ops,
							   void *factory_data, uint32_t sample_rate,
							   uint32_t channels, bool enabled)
{
	(void)ops;
	(void)factory_data;
	(void)sample_rate;
	(void)channels;
	struct mac_captions_core *core = calloc(1, sizeof(*core));
	if (core)
		core->enabled = enabled;
	return core;
}

void mac_captions_core_set_enabled(struct mac_captions_core *core, bool enabled)
{
	core->enabled = enabled;
}

struct obs_audio_data *mac_captions_core_filter_audio(struct mac_captions_core *core,
							     struct obs_audio_data *audio)
{
	(void)core;
	return audio;
}

bool mac_captions_core_drain_one(struct mac_captions_core *core,
					 void (*deliver)(void *param, const uint8_t *data), void *param)
{
	(void)core;
	(void)deliver;
	(void)param;
	return false;
}

bool mac_captions_core_is_running(struct mac_captions_core *core)
{
	(void)core;
	return false;
}

void mac_captions_core_destroy(struct mac_captions_core *core)
{
	free(core);
}
