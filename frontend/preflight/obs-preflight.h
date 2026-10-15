/* Hand-written C ABI for the Rust preflight engine. */
#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct obs_preflight_engine obs_preflight_engine_t;
typedef struct obs_preflight_bakeoff obs_preflight_bakeoff_t;

enum obs_preflight_status {
	OBS_PREFLIGHT_OK = 0,
	OBS_PREFLIGHT_INVALID_ARGUMENT = 1,
	OBS_PREFLIGHT_ANALYSIS_ERROR = 2,
	OBS_PREFLIGHT_OUT_OF_RANGE = 3,
	OBS_PREFLIGHT_COMPLETE = 4,
};

enum obs_preflight_verdict {
	OBS_PREFLIGHT_PASS = 0,
	OBS_PREFLIGHT_MARGINAL = 1,
	OBS_PREFLIGHT_FAIL = 2,
};

#define OBS_PREFLIGHT_REASON_CAPACITY 256
#define OBS_PREFLIGHT_ENCODER_ID_CAPACITY 128

struct obs_preflight_sample {
	uint64_t timestamp_ms;
	uint64_t average_frame_time_ns;
	uint64_t frame_interval_ns;
	uint32_t lagged_frames;
	uint64_t output_total_bytes;
	uint64_t output_total_frames;
	uint64_t output_frames_dropped;
	double output_congestion;
	int32_t connect_time_ms;
	uint64_t encoder_total_frames;
	uint64_t encoder_skipped_frames;
	double cpu_percent;
};

struct obs_preflight_window {
	uint64_t start_ms;
	uint64_t end_ms;
	uint64_t duration_ms;
	double output_bitrate_kbps;
	uint64_t output_bytes;
	uint64_t output_frames;
	uint64_t output_frames_dropped;
	double output_drop_percent;
	uint64_t encoder_frames;
	uint64_t encoder_frames_skipped;
	double encoder_skip_percent;
	uint64_t lagged_frames;
	double render_lag_percent;
	double average_frame_time_ns;
	double frame_interval_ns;
	int64_t render_headroom_ns;
	double congestion_percent;
	int32_t connect_time_ms;
	double cpu_percent;
	int32_t verdict;
	char reason[OBS_PREFLIGHT_REASON_CAPACITY];
};

struct obs_preflight_mode {
	uint32_t width;
	uint32_t height;
	uint32_t fps_num;
	uint32_t fps_den;
};

struct obs_preflight_candidate_plan {
	size_t order;
	char encoder[OBS_PREFLIGHT_ENCODER_ID_CAPACITY];
	uint32_t width;
	uint32_t height;
	uint32_t fps_num;
	uint32_t fps_den;
	uint64_t duration_ms;
	int32_t stop_if_fail;
};

/* Creates an engine with the default five-second analysis window. */
obs_preflight_engine_t *obs_preflight_engine_create(void);

/* Creates an engine with a custom non-zero window duration in milliseconds. */
obs_preflight_engine_t *obs_preflight_engine_create_with_window(uint64_t window_duration_ms);

/* Destroys an engine; passing NULL is safe. */
void obs_preflight_engine_destroy(obs_preflight_engine_t *engine);

/* Adds a timestamped sample. Counter decreases begin a new result window; the
 * interval crossing a reset is omitted. */
int32_t obs_preflight_engine_push_sample(obs_preflight_engine_t *engine, const struct obs_preflight_sample *sample);

/* Returns ANALYSIS_ERROR until at least two valid samples are available. */
int32_t obs_preflight_engine_get_window_count(const obs_preflight_engine_t *engine, size_t *count_out);

/* Copies a completed rollup and verdict into caller-owned storage. */
int32_t obs_preflight_engine_get_window(const obs_preflight_engine_t *engine, size_t index,
					struct obs_preflight_window *window_out);

/* Creates an encoder-major plan for every encoder ID and video mode. */
obs_preflight_bakeoff_t *obs_preflight_bakeoff_create(const char *const *encoder_ids, size_t encoder_count,
						      const struct obs_preflight_mode *modes, size_t mode_count,
						      uint64_t duration_ms);
void obs_preflight_bakeoff_destroy(obs_preflight_bakeoff_t *planner);
/* Writes the next candidate, or returns OBS_PREFLIGHT_COMPLETE after the last. */
int32_t obs_preflight_bakeoff_next(obs_preflight_bakeoff_t *planner,
				   struct obs_preflight_candidate_plan *candidate_out);
/* Records a pass/marginal/fail verdict and advances on the next call. */
int32_t obs_preflight_bakeoff_complete(obs_preflight_bakeoff_t *planner, int32_t verdict);

#ifdef __cplusplus
}
#endif
