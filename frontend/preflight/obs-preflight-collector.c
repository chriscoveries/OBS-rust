#include "obs-preflight-collector.h"

#include <util/platform.h>

#include <stdlib.h>

struct obs_preflight_collector {
	os_cpu_usage_info_t *cpu_info;
};

static uint64_t nonnegative_count(int count)
{
	return count > 0 ? (uint64_t)count : 0;
}

obs_preflight_collector_t *obs_preflight_collector_create(void)
{
	obs_preflight_collector_t *collector = calloc(1, sizeof(*collector));
	if (collector)
		collector->cpu_info = os_cpu_usage_info_start();
	return collector;
}

void obs_preflight_collector_destroy(obs_preflight_collector_t *collector)
{
	if (!collector)
		return;

	os_cpu_usage_info_destroy(collector->cpu_info);
	free(collector);
}

int32_t obs_preflight_collect_sample(obs_preflight_collector_t *collector, obs_preflight_engine_t *engine,
				     obs_output_t *output, video_t *video)
{
	if (!collector || !engine || !output || !video)
		return OBS_PREFLIGHT_INVALID_ARGUMENT;

	const uint64_t frame_interval_ns = obs_get_frame_interval_ns();
	if (!frame_interval_ns)
		return OBS_PREFLIGHT_ANALYSIS_ERROR;

	struct obs_preflight_sample sample = {
		.timestamp_ms = os_gettime_ns() / UINT64_C(1000000),
		.average_frame_time_ns = obs_get_average_frame_time_ns(),
		.frame_interval_ns = frame_interval_ns,
		.lagged_frames = obs_get_lagged_frames(),
		.output_total_bytes = obs_output_get_total_bytes(output),
		.output_total_frames = nonnegative_count(obs_output_get_total_frames(output)),
		.output_frames_dropped = nonnegative_count(obs_output_get_frames_dropped(output)),
		.output_congestion = obs_output_get_congestion(output),
		.connect_time_ms = obs_output_get_connect_time_ms(output),
		.encoder_total_frames = video_output_get_total_frames(video),
		.encoder_skipped_frames = video_output_get_skipped_frames(video),
		.cpu_percent = os_cpu_usage_info_query(collector->cpu_info),
	};

	return obs_preflight_engine_push_sample(engine, &sample);
}
