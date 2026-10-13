/* OBS-side sampler for the headless preflight engine. */
#pragma once

#include "obs-preflight.h"

#include <obs.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct obs_preflight_collector obs_preflight_collector_t;

/* Owns the OS CPU sampler; create one collector for each active test run. */
obs_preflight_collector_t *obs_preflight_collector_create(void);
void obs_preflight_collector_destroy(obs_preflight_collector_t *collector);

/* Reads one set of OBS counters and pushes it into engine. A decreased output
 * or video counter begins a new analysis window in the engine. */
int32_t obs_preflight_collect_sample(obs_preflight_collector_t *collector, obs_preflight_engine_t *engine,
				     obs_output_t *output, video_t *video);

#ifdef __cplusplus
}
#endif
