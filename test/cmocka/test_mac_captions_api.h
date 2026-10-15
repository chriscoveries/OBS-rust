#pragma once

#include <stdbool.h>
#include <stdint.h>

#include <obs.h>

struct mac_captions_transcriber_ops {
	void *(*create)(void *factory_data);
	bool (*start)(void *session, uint32_t sample_rate,
		      void (*word)(void *param, const char *text, double start_sec, double end_sec),
		      void *param);
	bool (*push)(void *session, const float *mono, uint32_t frames, uint64_t timestamp_ns);
	void (*stop)(void *session);
	void (*destroy)(void *session);
};

struct mac_captions_core;

struct mac_captions_core *mac_captions_core_create(const struct mac_captions_transcriber_ops *ops,
							   void *factory_data, uint32_t sample_rate,
							   uint32_t channels, bool enabled);
void mac_captions_core_set_enabled(struct mac_captions_core *core, bool enabled);
struct obs_audio_data *mac_captions_core_filter_audio(struct mac_captions_core *core,
							     struct obs_audio_data *audio);
bool mac_captions_core_drain_one(struct mac_captions_core *core,
					 void (*deliver)(void *param, const uint8_t *data), void *param);
bool mac_captions_core_is_running(struct mac_captions_core *core);
void mac_captions_core_destroy(struct mac_captions_core *core);
