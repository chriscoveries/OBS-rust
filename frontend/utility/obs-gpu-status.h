#pragma once

/* C ABI of rust/obs-gpu-status (src/ffi/mod.rs), linked into the frontend
 * only when ENABLE_RUST_LIBOBS is on (obs-rust/obs-studio#846). */

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Classify `adapter` (NULL when graphics init failed) against the GPUs
 * detected on the system (probed once per process). `frame_ns` holds `count`
 * one-second frame-time samples; with fewer than five, or interval_ns == 0,
 * frame timings are ignored. Writes 0 (green), 1 (yellow) or 2 (red) to
 * `state` when it is not NULL. Returns the gpuAcceleration JSON object; free
 * it with obs_gpu_status_free. */
char *obs_gpu_status_report(const char *adapter, const uint64_t *frame_ns, size_t count, uint64_t interval_ns,
			    int *state);

void obs_gpu_status_free(char *json);

#ifdef __cplusplus
}
#endif
