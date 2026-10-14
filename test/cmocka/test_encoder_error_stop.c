#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

/* Regression test for upstream obsproject/obs-studio#9946 (reported by
 * wly65535): when a video encoder fails mid-stream, libobs either deadlocks
 * or leaves an idle output that shares the encoder unable to start again.
 *
 * An encode error ends up in full_stop() (obs-encoder.c), which force-stops
 * every output connected to the encoder and then disconnects the encoder:
 *
 * - It also force-stops outputs that were never started, such as a replay
 *   buffer that is set up on the recording's encoders but not running.
 *   obs_output_actual_stop() resets the output's stopping event, and since the
 *   output never began data capture nothing signals it again. The next
 *   obs_output_start() of that output waits on the event forever.
 *
 * - For a texture encoder full_stop() runs on the GPU encode thread, and
 *   disconnecting the encoder (stop_gpu_encode()) waits for that thread to
 *   finish its frame: the thread waits on itself. The outputs on the encoder
 *   never finish stopping, and neither does anything that waits for them
 *   (stopping the recording, exiting OBS).
 *
 * The scenarios run the real replay_buffer output (obs-ffmpeg-mux.c is built
 * into the executable, as in test_replay_buffer_pause_stop) next to a
 * "recording" output that handles an encoder error the way ffmpeg_muxer does,
 * on a video encoder that fails exactly once. The raw-frame cases need no GPU
 * encoding. The texture cases use libobs-opengl's NV12 textures (software GL
 * is enough) and skip when the core video mix has no texture path.
 *
 * Two further cases cover the last GPU texture encoder stop racing a new start
 * on the same video mix. last_gpu_encoder_stop_serializes_same_mix_start
 * reproduces the original bug through public outputs only: the stale stop frees
 * the pool the new start just built, and the new encoder gets no frames or
 * cannot stop. gpu_encoder_teardown_wakes_every_waiting_start is specific to
 * the fix: it requires two starts to be inside the teardown condition wait at
 * once and both to resume. A build without that wait fails it with
 * RESULT_BROADCAST_NO_START_CONDVAR_WAIT, which is not a race reproduction.
 *
 * Each scenario runs in a child process with a watchdog, so a hang is a
 * deterministic failure. Core video needs a graphics module and libobs needs
 * an X display on Linux, so the tests skip without one (run under xvfb-run).
 * They need fork(), so they skip on Windows and macOS as well.
 */
#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <cmocka.h>

#include <stdio.h>
#include <stdlib.h>

#if !defined(_WIN32) && !defined(__APPLE__)
#include <dlfcn.h>
#include <errno.h>
#include <pthread.h>
#include <string.h>
#include <util/threading.h>
#include "obs-internal.h"
#endif

#if !defined(_WIN32) && !defined(__APPLE__)
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
#else
#define SCENARIO_UNSUPPORTED 1
#endif

#include <obs.h>
#include <obs-module.h>
#include <util/platform.h>
#include <util/threading.h>

extern struct obs_output_info replay_buffer;

/* obs-ffmpeg-mux.c looks up its UI strings through the module locale. */
const char *obs_module_text(const char *lookup)
{
	return lookup;
}

#define RAW_VENC_ID "test_err_raw_video_encoder"
#define TEX_VENC_ID "test_err_texture_video_encoder"
#define AENC_ID "test_err_audio_encoder"
#define RECORDING_ID "test_err_recording"

/* How long a start or stop may take. A healthy one takes a few frames. */
#define STEP_TIMEOUT_MS 5000
/* Watchdog for the whole child, in case a step blocks the calling thread. */
#define CHILD_TIMEOUT_MS 40000
/* Repeat the race while bounding both each child and the complete test. */
#define GPU_STOP_START_RACE_ATTEMPTS 3
#define GPU_STOP_START_RACE_CHILD_TIMEOUT_MS 15000
#define GPU_STOP_START_RACE_TOTAL_TIMEOUT_MS 20000
#define GPU_STOP_START_RACE_START_WINDOW_MS 1000
/* Time for A's worker join to reach the graphics lock behind B. */
#define GPU_STOP_START_RACE_A_JOIN_WINDOW_MS 100
/* The encoder fails on this encode call after it is armed. */
#define FAIL_AFTER_FRAMES 10

enum result {
	RESULT_OK = 0,
	RESULT_SETUP_FAILED = 1,
	RESULT_NO_TEXTURE_PATH = 2,
	RESULT_NO_PACKETS = 3,
	RESULT_NO_FAILURE = 4,
	RESULT_STOP_HUNG = 5,
	RESULT_START_HUNG = 6,
	RESULT_START_FAILED = 7,
	RESULT_NOT_ENCODING = 8,
	RESULT_SHUTDOWN_HUNG = 9,
	RESULT_ENCODER_LEAKED = 10,
	RESULT_STOP_FINISHED_EARLY = 13,
	RESULT_GPU_ERROR_STOP_HUNG = 14,
	RESULT_RACE_STOP_HUNG = 15,
	RESULT_RACE_START_HUNG = 16,
	RESULT_RACE_B_STOP_HUNG = 17,
	RESULT_MIX_INACTIVE_DURING_DRAIN = 18,
	RESULT_RACE_FRAME_FAILED = 19,
	RESULT_RACE_A_END_CAPTURE_HUNG = 21,
	RESULT_RACE_B_END_CAPTURE_HUNG = 24,
	RESULT_LAST_STOP_NOT_REACHED = 28,
	/* gpu_encoder_teardown_wakes_every_waiting_start only (fix-specific) */
	RESULT_BROADCAST_NO_START_CONDVAR_WAIT = 30,
	RESULT_BROADCAST_WAITERS_MISSING = 31,
	RESULT_BROADCAST_STARTS_STALLED = 32,
	RESULT_BROADCAST_A_END_CAPTURE_HUNG = 33,
	RESULT_BROADCAST_B_START_HUNG = 34,
	RESULT_BROADCAST_C_START_HUNG = 35,
	RESULT_BROADCAST_START_FAILED = 36,
	RESULT_BROADCAST_B_FRAME_FAILED = 37,
	RESULT_BROADCAST_C_FRAME_FAILED = 38,
	RESULT_BROADCAST_B_STOP_HUNG = 39,
	RESULT_BROADCAST_C_STOP_HUNG = 40,
};

static const char *result_name(int r)
{
	switch (r) {
	case RESULT_OK:
		return "ok";
	case RESULT_SETUP_FAILED:
		return "setup failed";
	case RESULT_NO_TEXTURE_PATH:
		return "no texture encoding path";
	case RESULT_NO_PACKETS:
		return "no packets before the failure";
	case RESULT_NO_FAILURE:
		return "the encoder never failed";
	case RESULT_STOP_HUNG:
		return "the recording never finished stopping after the encoder error";
	case RESULT_START_HUNG:
		return "obs_output_start() hung after the encoder error";
	case RESULT_START_FAILED:
		return "obs_output_start() failed after the encoder error";
	case RESULT_NOT_ENCODING:
		return "the restarted output received no frames";
	case RESULT_SHUTDOWN_HUNG:
		return "stopping or releasing the outputs hung";
	case RESULT_ENCODER_LEAKED:
		return "the video encoder was never destroyed (a thread still holds it)";
	case RESULT_STOP_FINISHED_EARLY:
		return "the output stopped before the in-flight texture callback completed";
	case RESULT_GPU_ERROR_STOP_HUNG:
		return "the output did not finish stopping after the GPU encoder error";
	case RESULT_RACE_STOP_HUNG:
		return "encoder A did not finish stopping after the callback was released";
	case RESULT_RACE_START_HUNG:
		return "encoder B did not finish starting after encoder A released the pool";
	case RESULT_RACE_B_STOP_HUNG:
		return "encoder B did not finish stopping after encoder A's teardown";
	case RESULT_MIX_INACTIVE_DURING_DRAIN:
		return "video mix became inactive while its GPU worker was still draining";
	case RESULT_RACE_FRAME_FAILED:
		return "encoder B did not receive frames after encoder A finished end-capture";
	case RESULT_RACE_A_END_CAPTURE_HUNG:
		return "encoder A did not finish asynchronous end-capture after its callback was released";
	case RESULT_RACE_B_END_CAPTURE_HUNG:
		return "encoder B did not finish asynchronous end-capture";
	case RESULT_LAST_STOP_NOT_REACHED:
		return "encoder A's stop never removed it from the video mix's GPU encoder list";
	case RESULT_BROADCAST_NO_START_CONDVAR_WAIT:
		return "no GPU encoder start waited on the video mix's teardown condition variable: a start "
		       "completed while encoder A's teardown was held (this build has no start-side teardown "
		       "wait; fix-specific check, not a reproduction of the stop/start race)";
	case RESULT_BROADCAST_WAITERS_MISSING:
		return "encoders B and C were not both inside the same teardown condition wait, with its mutex "
		       "released, before encoder A's teardown was allowed to finish";
	case RESULT_BROADCAST_STARTS_STALLED:
		return "neither encoder B nor C reached the teardown condition wait or returned from its start";
	case RESULT_BROADCAST_A_END_CAPTURE_HUNG:
		return "encoder A did not finish asynchronous end-capture after its callback was released";
	case RESULT_BROADCAST_B_START_HUNG:
		return "encoder B was not woken from the teardown wait (its start never returned)";
	case RESULT_BROADCAST_C_START_HUNG:
		return "encoder C was not woken from the teardown wait (its start never returned)";
	case RESULT_BROADCAST_START_FAILED:
		return "encoder B or C failed to start after the teardown wait";
	case RESULT_BROADCAST_B_FRAME_FAILED:
		return "encoder B did not receive frames after waking from the teardown wait";
	case RESULT_BROADCAST_C_FRAME_FAILED:
		return "encoder C did not receive frames after waking from the teardown wait";
	case RESULT_BROADCAST_B_STOP_HUNG:
		return "encoder B did not finish asynchronous end-capture";
	case RESULT_BROADCAST_C_STOP_HUNG:
		return "encoder C did not finish asynchronous end-capture";
	default:
		return "unknown";
	}
}

/* ------------------------------------------------------------------------- */
/* encoders */

static volatile long video_packets;
static volatile long texture_encodes;
static volatile long failures;
static volatile long venc_creates;
static volatile long venc_destroys;
static obs_encoder_t *watched_texture_encoders[2];
static volatile long watched_texture_encodes[2];

#if !defined(_WIN32) && !defined(__APPLE__)
enum teardown_wait_role {
	TEARDOWN_WAIT_NONE,
	TEARDOWN_WAIT_ENCODER_B,
	TEARDOWN_WAIT_ENCODER_C,
};

/* The video mix whose teardown wait is observed. Written by the main thread
 * before it creates the B and C start threads (pthread_create orders it), and
 * read only on those role-tagged threads. */
static struct obs_core_video_mix *watched_teardown_mix;
static pthread_cond_t *start_wait_cond_b;
static pthread_cond_t *start_wait_cond_c;
static pthread_mutex_t *start_wait_mutex_b;
static pthread_mutex_t *start_wait_mutex_c;
static volatile long start_waiters_b;
static volatile long start_waiters_c;
static _Thread_local enum teardown_wait_role teardown_wait_role;
static pthread_once_t resolve_cond_wait_once = PTHREAD_ONCE_INIT;
static int (*real_pthread_cond_wait)(pthread_cond_t *, pthread_mutex_t *);

static void resolve_pthread_cond_wait(void)
{
	void *symbol = dlsym(RTLD_NEXT, "pthread_cond_wait");
	memcpy(&real_pthread_cond_wait, &symbol, sizeof(real_pthread_cond_wait));
}

static bool is_member_of_mix(const struct obs_core_video_mix *mix, const void *member, size_t size)
{
	uintptr_t begin = (uintptr_t)mix;
	uintptr_t address = (uintptr_t)member;
	return address >= begin && size <= sizeof(*mix) && address - begin <= sizeof(*mix) - size;
}

/* Observe the per-mix GPU encoder teardown wait without a libobs test hook.
 * libobs is built with hidden visibility, so start_gpu_encode() is not in its
 * dynamic symbol table and the caller cannot be named with dladdr(). Instead
 * the wait is identified by its objects: a wait on a pthread_cond_t that is a
 * member of struct obs_core_video_mix. The only such member is the teardown
 * condition variable added by the fix, and its only waiter is
 * start_gpu_encode() in obs.c. That code is C in both ENABLE_RUST_LIBOBS
 * modes and calls pthread_cond_wait() directly, so this wrapper sees it in
 * both. Without the fix the mix has no condition variable, and a start never
 * records a wait here. os_event_wait() (a Rust futex Condvar with Rust ON) is
 * deliberately not observed.
 *
 * The project builds with hidden visibility, which also applies to this
 * executable, so the wrapper must be exported explicitly: otherwise it is a
 * local symbol and libobs binds to the C library's pthread_cond_wait. */
__attribute__((visibility("default"))) int pthread_cond_wait(pthread_cond_t *cond, pthread_mutex_t *mutex)
{
	if (pthread_once(&resolve_cond_wait_once, resolve_pthread_cond_wait) != 0 || !real_pthread_cond_wait)
		return EINVAL;

	enum teardown_wait_role role = teardown_wait_role;
	struct obs_core_video_mix *mix = role != TEARDOWN_WAIT_NONE ? watched_teardown_mix : NULL;
	if (mix && is_member_of_mix(mix, cond, sizeof(*cond)) && is_member_of_mix(mix, mutex, sizeof(*mutex)) &&
	    mutex != &mix->gpu_encoder_mutex) {
		/* Each pointer pair has one writer, published by the counter. */
		if (role == TEARDOWN_WAIT_ENCODER_B) {
			if (!os_atomic_load_long(&start_waiters_b)) {
				start_wait_cond_b = cond;
				start_wait_mutex_b = mutex;
			}
			os_atomic_inc_long(&start_waiters_b);
		} else if (role == TEARDOWN_WAIT_ENCODER_C) {
			if (!os_atomic_load_long(&start_waiters_c)) {
				start_wait_cond_c = cond;
				start_wait_mutex_c = mutex;
			}
			os_atomic_inc_long(&start_waiters_c);
		}
	}

	return real_pthread_cond_wait(cond, mutex);
}
#endif
/* encode calls left until the one that fails, -1 when not armed */
static volatile long fail_in = -1;
static volatile bool block_next_texture_encode;
static volatile bool fail_blocked_texture_encode;
static os_event_t *texture_encode_gate_entered;
static os_event_t *release_texture_encode_gate;
static uint8_t packet_byte;

static bool encode_should_fail(void)
{
	if (os_atomic_load_long(&fail_in) < 0)
		return false;
	if (os_atomic_dec_long(&fail_in) != 0)
		return false;
	os_atomic_inc_long(&failures);
	return true;
}

static void make_video_packet(struct encoder_packet *packet, int64_t pts, bool *received)
{
	packet->data = &packet_byte;
	packet->size = 1;
	packet->pts = pts;
	packet->dts = pts;
	packet->type = OBS_ENCODER_VIDEO;
	packet->keyframe = true;
	*received = true;
	os_atomic_inc_long(&video_packets);
}

static const char *enc_name(void *type_data)
{
	UNUSED_PARAMETER(type_data);
	return "test encoder";
}

static void *enc_create(obs_data_t *settings, obs_encoder_t *encoder)
{
	UNUSED_PARAMETER(settings);
	return encoder;
}

static void enc_destroy(void *data)
{
	UNUSED_PARAMETER(data);
}

static void *venc_create(obs_data_t *settings, obs_encoder_t *encoder)
{
	UNUSED_PARAMETER(settings);
	os_atomic_inc_long(&venc_creates);
	return encoder;
}

static void venc_destroy(void *data)
{
	UNUSED_PARAMETER(data);
	os_atomic_inc_long(&venc_destroys);
}

static bool raw_venc_encode(void *data, struct encoder_frame *frame, struct encoder_packet *packet, bool *received)
{
	UNUSED_PARAMETER(data);
	if (encode_should_fail())
		return false;
	make_video_packet(packet, frame->pts, received);
	return true;
}

static bool tex_venc_encode(void *data, struct encoder_texture *texture, int64_t pts, uint64_t lock_key,
			    uint64_t *next_key, struct encoder_packet *packet, bool *received)
{
	UNUSED_PARAMETER(data);
	UNUSED_PARAMETER(texture);
	UNUSED_PARAMETER(lock_key);
	UNUSED_PARAMETER(next_key);
	if (os_atomic_exchange_bool(&block_next_texture_encode, false)) {
		os_event_signal(texture_encode_gate_entered);
		os_event_wait(release_texture_encode_gate);
		if (os_atomic_exchange_bool(&fail_blocked_texture_encode, false))
			return false;
	}
	os_atomic_inc_long(&texture_encodes);
	for (size_t i = 0; i < sizeof(watched_texture_encoders) / sizeof(watched_texture_encoders[0]); i++) {
		if (data == watched_texture_encoders[i])
			os_atomic_inc_long(&watched_texture_encodes[i]);
	}
	if (encode_should_fail())
		return false;
	make_video_packet(packet, pts, received);
	return true;
}

static bool aenc_encode(void *data, struct encoder_frame *frame, struct encoder_packet *packet, bool *received)
{
	UNUSED_PARAMETER(data);
	packet->data = &packet_byte;
	packet->size = 1;
	packet->pts = frame->pts;
	packet->dts = frame->pts;
	packet->type = OBS_ENCODER_AUDIO;
	*received = true;
	return true;
}

static size_t aenc_frame_size(void *data)
{
	UNUSED_PARAMETER(data);
	return 1024;
}

static void register_encoders(void)
{
	struct obs_encoder_info raw = {
		.id = RAW_VENC_ID,
		.type = OBS_ENCODER_VIDEO,
		.codec = "h264",
		.get_name = enc_name,
		.create = venc_create,
		.destroy = venc_destroy,
		.encode = raw_venc_encode,
	};
	obs_register_encoder(&raw);

	/* texture only, like a hardware encoder: libobs refuses to start it
	 * when the video mix has no texture path */
	struct obs_encoder_info tex = {
		.id = TEX_VENC_ID,
		.type = OBS_ENCODER_VIDEO,
		.codec = "h264",
		.get_name = enc_name,
		.create = venc_create,
		.destroy = venc_destroy,
		.encode_texture2 = tex_venc_encode,
		.caps = OBS_ENCODER_CAP_PASS_TEXTURE,
	};
	obs_register_encoder(&tex);

	struct obs_encoder_info aenc = {
		.id = AENC_ID,
		.type = OBS_ENCODER_AUDIO,
		.codec = "aac",
		.get_name = enc_name,
		.create = enc_create,
		.destroy = enc_destroy,
		.encode = aenc_encode,
		.get_frame_size = aenc_frame_size,
	};
	obs_register_encoder(&aenc);
}

/* ------------------------------------------------------------------------- */
/* a recording output that handles an encoder error like ffmpeg_muxer: the
 * NULL packet that full_stop() sends stops it with OBS_OUTPUT_ENCODE_ERROR */

struct recording {
	obs_output_t *output;
	volatile bool active;
};

static const char *rec_name(void *type_data)
{
	UNUSED_PARAMETER(type_data);
	return "test recording";
}

static void *rec_create(obs_data_t *settings, obs_output_t *output)
{
	UNUSED_PARAMETER(settings);
	struct recording *rec = bzalloc(sizeof(*rec));
	rec->output = output;
	return rec;
}

static void rec_destroy(void *data)
{
	bfree(data);
}

static bool rec_start(void *data)
{
	struct recording *rec = data;
	if (!obs_output_can_begin_data_capture(rec->output, 0))
		return false;
	if (!obs_output_initialize_encoders(rec->output, 0))
		return false;
	os_atomic_set_bool(&rec->active, true);
	return obs_output_begin_data_capture(rec->output, 0);
}

static void rec_stop(void *data, uint64_t ts)
{
	struct recording *rec = data;
	/* a forced stop (ts 0) finishes on the NULL packet that follows it,
	 * like ffmpeg_muxer */
	if (ts && os_atomic_exchange_bool(&rec->active, false))
		obs_output_end_data_capture(rec->output);
}

static void rec_packet(void *data, struct encoder_packet *packet)
{
	struct recording *rec = data;
	if (packet)
		return;
	if (os_atomic_exchange_bool(&rec->active, false))
		obs_output_signal_stop(rec->output, OBS_OUTPUT_ENCODE_ERROR);
}

static void register_outputs(void)
{
	struct obs_output_info rec = {
		.id = RECORDING_ID,
		.flags = OBS_OUTPUT_AV | OBS_OUTPUT_ENCODED,
		.get_name = rec_name,
		.create = rec_create,
		.destroy = rec_destroy,
		.start = rec_start,
		.stop = rec_stop,
		.encoded_packet = rec_packet,
	};
	obs_register_output(&rec);
	obs_register_output(&replay_buffer);
}

/* ------------------------------------------------------------------------- */

static int obs_up;

#ifndef SCENARIO_UNSUPPORTED

enum scenario {
	/* raw frames, the replay buffer is set up but not running */
	SCENARIO_RAW_IDLE_REPLAY_BUFFER,
	/* raw frames, the replay buffer is running too (control) */
	SCENARIO_RAW_ACTIVE_REPLAY_BUFFER,
	/* texture encoder, recording only */
	SCENARIO_TEXTURE_RECORDING,
	/* texture encoder, the replay buffer is set up but not running */
	SCENARIO_TEXTURE_IDLE_REPLAY_BUFFER,
};

static bool init_core(void)
{
	if (!obs_startup("en-US", NULL, NULL))
		return false;

	/* GPU conversion to NV12 gives the core video mix NV12 textures, so
	 * texture encoders get the GPU encode thread */
	struct obs_video_info ovi = {
		.graphics_module = GRAPHICS_MODULE_PATH,
		.fps_num = 30,
		.fps_den = 1,
		.base_width = 64,
		.base_height = 64,
		.output_width = 64,
		.output_height = 64,
		.output_format = VIDEO_FORMAT_NV12,
		.colorspace = VIDEO_CS_709,
		.range = VIDEO_RANGE_PARTIAL,
		.adapter = 0,
		.gpu_conversion = true,
		.scale_type = OBS_SCALE_BICUBIC,
	};
	int ret = obs_reset_video(&ovi);
	if (ret != OBS_VIDEO_SUCCESS) {
		fprintf(stderr, "obs_reset_video failed: %d\n", ret);
		return false;
	}

	struct obs_audio_info oai = {.samples_per_sec = 48000, .speakers = SPEAKERS_STEREO};
	if (!obs_reset_audio(&oai)) {
		fprintf(stderr, "obs_reset_audio failed\n");
		return false;
	}
	return true;
}

static bool wait_inactive(obs_output_t *output, int timeout_ms)
{
	for (int i = 0; i < timeout_ms / 10; i++) {
		if (!obs_output_active(output))
			return true;
		os_sleep_ms(10);
	}
	return !obs_output_active(output);
}

static bool wait_counter(volatile long *counter, long target, int timeout_ms)
{
	for (int i = 0; i < timeout_ms / 10; i++) {
		if (os_atomic_load_long(counter) >= target)
			return true;
		os_sleep_ms(10);
	}
	return os_atomic_load_long(counter) >= target;
}

#if !defined(_WIN32) && !defined(__APPLE__)
/* The core video mix that obs_get_video() and its encoders use. */
static struct obs_core_video_mix *main_video_mix(void)
{
	obs_canvas_t *canvas = obs_get_main_canvas();
	struct obs_core_video_mix *mix = canvas ? canvas->mix : NULL;
	obs_canvas_release(canvas);
	return mix && mix->video == obs_get_video() ? mix : NULL;
}

/* Wait until the last GPU encoder's stop has removed it from the mix, under
 * the mix's own GPU encoder mutex. The caller holds that encoder in its texture
 * callback, so the stop has then decided to free the pool and is waiting for
 * the worker. With the fix, the same critical section also marks teardown, so a
 * start that follows must wait. This observes list state that exists with and
 * without the fix; it does not depend on how the worker wait is implemented. */
static bool wait_for_last_gpu_encoder_removed(struct obs_core_video_mix *mix, int timeout_ms)
{
	for (int i = 0; i < timeout_ms; i++) {
		pthread_mutex_lock(&mix->gpu_encoder_mutex);
		bool removed = mix->gpu_encoders.num == 0;
		pthread_mutex_unlock(&mix->gpu_encoder_mutex);
		if (removed)
			return true;
		os_sleep_ms(1);
	}
	return false;
}

static void reset_start_wait_observations(struct obs_core_video_mix *mix)
{
	watched_teardown_mix = mix;
	start_wait_cond_b = NULL;
	start_wait_cond_c = NULL;
	start_wait_mutex_b = NULL;
	start_wait_mutex_c = NULL;
	os_atomic_set_long(&start_waiters_b, 0);
	os_atomic_set_long(&start_waiters_c, 0);
}

static bool wait_for_mutex_release(pthread_mutex_t *mutex, int timeout_ms)
{
	for (int i = 0; i < timeout_ms; i++) {
		int ret = pthread_mutex_trylock(mutex);
		if (ret == 0) {
			pthread_mutex_unlock(mutex);
			return true;
		}
		if (ret != EBUSY)
			return false;
		os_sleep_ms(1);
	}
	return false;
}
#endif

/* obs_output_start() on a worker thread, so that a hang is reported */
struct start_job {
	obs_output_t *output;
	os_event_t *done;
	bool success;
};

static void *start_thread(void *param)
{
	struct start_job *job = param;
	job->success = obs_output_start(job->output);
	os_event_signal(job->done);
	return NULL;
}

static enum result start_with_timeout(obs_output_t *output)
{
	struct start_job job = {.output = output};
	pthread_t thread;
	if (os_event_init(&job.done, OS_EVENT_TYPE_MANUAL) != 0 ||
	    pthread_create(&thread, NULL, start_thread, &job) != 0)
		return RESULT_SETUP_FAILED;
	if (os_event_timedwait(job.done, STEP_TIMEOUT_MS) != 0)
		return RESULT_START_HUNG;
	pthread_join(thread, NULL);
	os_event_destroy(job.done);
	if (!job.success) {
		fprintf(stderr, "start of '%s' failed: %s\n", obs_output_get_name(output),
			obs_output_get_last_error(output));
		return RESULT_START_FAILED;
	}
	return RESULT_OK;
}

/* stop an output, failing instead of hanging */
static enum result stop_with_timeout(obs_output_t *output)
{
	obs_output_stop(output);
	return wait_inactive(output, STEP_TIMEOUT_MS) ? RESULT_OK : RESULT_SHUTDOWN_HUNG;
}

/* the started output works again: video packets reach it */
static enum result check_encoding(obs_output_t *output)
{
	for (int i = 0; i < STEP_TIMEOUT_MS / 10; i++) {
		if (obs_output_get_total_frames(output) >= 15)
			break;
		os_sleep_ms(10);
	}
	if (obs_output_get_total_frames(output) < 15 || !obs_output_active(output)) {
		fprintf(stderr, "'%s' received %d frames\n", obs_output_get_name(output),
			obs_output_get_total_frames(output));
		return RESULT_NOT_ENCODING;
	}
	return RESULT_OK;
}

static enum result run_scenario(enum scenario scenario)
{
	bool texture = scenario == SCENARIO_TEXTURE_RECORDING || scenario == SCENARIO_TEXTURE_IDLE_REPLAY_BUFFER;
	bool with_rb = scenario != SCENARIO_TEXTURE_RECORDING;
	bool rb_running = scenario == SCENARIO_RAW_ACTIVE_REPLAY_BUFFER;
	enum result r;

	if (!init_core())
		return RESULT_SETUP_FAILED;
	register_encoders();
	register_outputs();

	obs_encoder_t *venc = obs_video_encoder_create(texture ? TEX_VENC_ID : RAW_VENC_ID, "venc", NULL, NULL);
	obs_encoder_t *aenc = obs_audio_encoder_create(AENC_ID, "aenc", NULL, 0, NULL);
	obs_output_t *rec = obs_output_create(RECORDING_ID, "recording", NULL, NULL);
	obs_output_t *rb = with_rb ? obs_output_create("replay_buffer", "replay buffer", NULL, NULL) : NULL;
	if (!venc || !aenc || !rec || (with_rb && !rb))
		return RESULT_SETUP_FAILED;

	obs_encoder_set_video(venc, obs_get_video());
	obs_encoder_set_audio(aenc, obs_get_audio());
	obs_output_set_video_encoder(rec, venc);
	obs_output_set_audio_encoder(rec, aenc, 0);
	if (rb) {
		obs_output_set_video_encoder(rb, venc);
		obs_output_set_audio_encoder(rb, aenc, 0);
	}

	if (!obs_output_start(rec)) {
		fprintf(stderr, "recording failed to start: %s\n", obs_output_get_last_error(rec));
		/* a texture-only encoder is refused without a texture path */
		return texture ? RESULT_NO_TEXTURE_PATH : RESULT_SETUP_FAILED;
	}
	if (rb_running && !obs_output_start(rb)) {
		fprintf(stderr, "replay buffer failed to start\n");
		return RESULT_SETUP_FAILED;
	}
	if (!wait_counter(&video_packets, 15, STEP_TIMEOUT_MS))
		return RESULT_NO_PACKETS;
	if (texture && !os_atomic_load_long(&texture_encodes))
		return RESULT_NO_TEXTURE_PATH;

	/* the encoder fails mid-recording, like NVENC after a driver update */
	os_atomic_set_long(&fail_in, FAIL_AFTER_FRAMES);
	if (!wait_counter(&failures, 1, STEP_TIMEOUT_MS))
		return RESULT_NO_FAILURE;

	if (!wait_inactive(rec, STEP_TIMEOUT_MS))
		return RESULT_STOP_HUNG;
	if (rb_running && !wait_inactive(rb, STEP_TIMEOUT_MS))
		return RESULT_STOP_HUNG;

	/* the outputs on the failed encoder can start again, and the encoder
	 * (and for a texture encoder, the GPU encode thread) works again */
	obs_output_t *restart = rb ? rb : rec;
	r = start_with_timeout(restart);
	if (r != RESULT_OK)
		return r;
	r = check_encoding(restart);
	if (r != RESULT_OK)
		return r;
	if (texture && rb) {
		/* the recording too, next to the replay buffer */
		r = start_with_timeout(rec);
		if (r != RESULT_OK)
			return r;
		r = check_encoding(rec);
		if (r != RESULT_OK)
			return r;
		r = stop_with_timeout(rec);
		if (r != RESULT_OK)
			return r;
	}
	r = stop_with_timeout(restart);
	if (r != RESULT_OK)
		return r;

	obs_weak_encoder_t *weak_venc = obs_encoder_get_weak_encoder(venc);
	if (rb)
		obs_output_release(rb);
	obs_output_release(rec);
	obs_encoder_release(venc);
	obs_encoder_release(aenc);

	/* nothing may still hold the encoder, such as a GPU encode thread
	 * that never finished the frame it failed on */
	if (!wait_counter(&venc_destroys, os_atomic_load_long(&venc_creates), STEP_TIMEOUT_MS))
		return RESULT_ENCODER_LEAKED;
	bool released = false;
	for (int i = 0; !released && i < STEP_TIMEOUT_MS / 10; i++) {
		obs_encoder_t *still = obs_weak_encoder_get_encoder(weak_venc);
		released = !still;
		obs_encoder_release(still);
		if (!released)
			os_sleep_ms(10);
	}
	obs_weak_encoder_release(weak_venc);
	if (!released)
		return RESULT_ENCODER_LEAKED;

	obs_shutdown();
	return RESULT_OK;
}

static int run_in_child_timeout(int (*fn)(int), int arg, int timeout_ms)
{
	fflush(stderr);
	pid_t pid = fork();
	if (pid == 0) {
		signal(SIGSEGV, SIG_DFL);
		signal(SIGBUS, SIG_DFL);
		signal(SIGILL, SIG_DFL);
		signal(SIGFPE, SIG_DFL);
		signal(SIGABRT, SIG_DFL);
		/* a hung output cannot be torn down: leave without cleanup */
		_exit(fn(arg));
	}
	if (pid < 0)
		return -1;

	int status = 0;
	pid_t r = 0;
	for (int i = 0; i < timeout_ms / 10; i++) {
		r = waitpid(pid, &status, WNOHANG);
		if (r != 0)
			break;
		os_sleep_ms(10);
	}
	if (r == 0) {
		kill(pid, SIGKILL);
		waitpid(pid, &status, 0);
		return -2;
	}
	if (WIFSIGNALED(status))
		return -100 - WTERMSIG(status);
	return WEXITSTATUS(status);
}

static int run_in_child(int (*fn)(int), int arg)
{
	return run_in_child_timeout(fn, arg, CHILD_TIMEOUT_MS);
}

static uint64_t monotonic_time_ms(void)
{
	struct timespec now;
	clock_gettime(CLOCK_MONOTONIC, &now);
	return (uint64_t)now.tv_sec * 1000 + (uint64_t)now.tv_nsec / 1000000;
}

struct stop_start_race {
	obs_output_t *output_a;
	obs_output_t *output_b;
	obs_output_t *output_c;
	os_event_t *start_attempted;
	os_event_t *c_start_attempted;
	os_event_t *stop_done;
	os_event_t *start_done;
	os_event_t *c_start_done;
	os_event_t *b_stop_done;
	os_event_t *c_stop_done;
	bool start_success;
	bool c_start_success;
	volatile long start_returned_b;
	volatile long start_returned_c;
};

static void *stop_output_thread(void *param)
{
	struct stop_start_race *race = param;
	obs_output_stop(race->output_a);
	os_event_signal(race->stop_done);
	return NULL;
}

static void *start_output_thread(void *param)
{
	struct stop_start_race *race = param;
	os_event_signal(race->start_attempted);
#if !defined(_WIN32) && !defined(__APPLE__)
	teardown_wait_role = TEARDOWN_WAIT_ENCODER_B;
#endif
	race->start_success = obs_output_start(race->output_b);
#if !defined(_WIN32) && !defined(__APPLE__)
	teardown_wait_role = TEARDOWN_WAIT_NONE;
#endif
	os_atomic_inc_long(&race->start_returned_b);
	os_event_signal(race->start_done);
	return NULL;
}

static void *start_output_c_thread(void *param)
{
	struct stop_start_race *race = param;
	os_event_signal(race->c_start_attempted);
#if !defined(_WIN32) && !defined(__APPLE__)
	teardown_wait_role = TEARDOWN_WAIT_ENCODER_C;
#endif
	race->c_start_success = obs_output_start(race->output_c);
#if !defined(_WIN32) && !defined(__APPLE__)
	teardown_wait_role = TEARDOWN_WAIT_NONE;
#endif
	os_atomic_inc_long(&race->start_returned_c);
	os_event_signal(race->c_start_done);
	return NULL;
}

static void *stop_output_b_thread(void *param)
{
	struct stop_start_race *race = param;
	obs_output_stop(race->output_b);
	os_event_signal(race->b_stop_done);
	return NULL;
}

static void *stop_output_c_thread(void *param)
{
	struct stop_start_race *race = param;
	obs_output_stop(race->output_c);
	os_event_signal(race->c_stop_done);
	return NULL;
}

static int stop_start_race_child(int unused)
{
	UNUSED_PARAMETER(unused);
	if (!init_core())
		return RESULT_SETUP_FAILED;
	register_encoders();
	register_outputs();

	obs_encoder_t *encoder_a = obs_video_encoder_create(TEX_VENC_ID, "encoder A", NULL, NULL);
	obs_encoder_t *encoder_b = obs_video_encoder_create(TEX_VENC_ID, "encoder B", NULL, NULL);
	obs_encoder_t *audio_encoder = obs_audio_encoder_create(AENC_ID, "audio", NULL, 0, NULL);
	obs_output_t *output_a = obs_output_create(RECORDING_ID, "output A", NULL, NULL);
	obs_output_t *output_b = obs_output_create(RECORDING_ID, "output B", NULL, NULL);
	if (!encoder_a || !encoder_b || !audio_encoder || !output_a || !output_b)
		return RESULT_SETUP_FAILED;

	obs_encoder_set_video(encoder_a, obs_get_video());
	obs_encoder_set_video(encoder_b, obs_get_video());
	obs_encoder_set_audio(audio_encoder, obs_get_audio());
	obs_output_set_video_encoder(output_a, encoder_a);
	obs_output_set_video_encoder(output_b, encoder_b);
	obs_output_set_audio_encoder(output_a, audio_encoder, 0);
	obs_output_set_audio_encoder(output_b, audio_encoder, 0);
	/* Publish before A's GPU callbacks begin reading the watcher array. */
	watched_texture_encoders[0] = encoder_b;
	if (!obs_output_start(output_a)) {
		fprintf(stderr, "output A failed to start: %s\n", obs_output_get_last_error(output_a));
		return RESULT_NO_TEXTURE_PATH;
	}
	if (!wait_counter(&texture_encodes, 3, STEP_TIMEOUT_MS))
		return RESULT_NO_TEXTURE_PATH;

	struct stop_start_race race = {.output_a = output_a, .output_b = output_b};
	if (os_event_init(&race.start_attempted, OS_EVENT_TYPE_MANUAL) != 0 ||
	    os_event_init(&race.stop_done, OS_EVENT_TYPE_MANUAL) != 0 ||
	    os_event_init(&race.start_done, OS_EVENT_TYPE_MANUAL) != 0 ||
	    os_event_init(&race.b_stop_done, OS_EVENT_TYPE_MANUAL) != 0 ||
	    os_event_init(&texture_encode_gate_entered, OS_EVENT_TYPE_MANUAL) != 0 ||
	    os_event_init(&release_texture_encode_gate, OS_EVENT_TYPE_MANUAL) != 0)
		return RESULT_SETUP_FAILED;

	os_atomic_set_bool(&block_next_texture_encode, true);
	os_atomic_set_bool(&fail_blocked_texture_encode, false);
	if (os_event_timedwait(texture_encode_gate_entered, STEP_TIMEOUT_MS) != 0)
		return RESULT_NO_TEXTURE_PATH;

	pthread_t stop_thread;
	if (pthread_create(&stop_thread, NULL, stop_output_thread, &race) != 0)
		return RESULT_SETUP_FAILED;
	if (os_event_timedwait(race.stop_done, STEP_TIMEOUT_MS) != 0)
		return RESULT_SETUP_FAILED;
	/* obs_output_stop() returns before end-capture disconnects A. Wait until A's
	 * last stop has removed it from the list: the stop has then decided to free
	 * the pool and is waiting for the worker, which A's held callback blocks.
	 * This is the stale-free window of the original bug, with or without the
	 * fix. No assertion below depends on how that worker wait is implemented. */
	struct obs_core_video_mix *mix = main_video_mix();
	if (!mix || !wait_for_last_gpu_encoder_removed(mix, STEP_TIMEOUT_MS)) {
		os_event_signal(release_texture_encode_gate);
		pthread_join(stop_thread, NULL);
		return mix ? RESULT_LAST_STOP_NOT_REACHED : RESULT_SETUP_FAILED;
	}

	/* Hold graphics so B's start queues before A's post-drain pool free. On
	 * master both wait for this lock; the fixed path keeps B behind teardown. */
	obs_enter_graphics();
	pthread_t start_thread;
	if (pthread_create(&start_thread, NULL, start_output_thread, &race) != 0) {
		os_event_signal(release_texture_encode_gate);
		obs_leave_graphics();
		pthread_join(stop_thread, NULL);
		return RESULT_SETUP_FAILED;
	}
	if (os_event_timedwait(race.start_attempted, STEP_TIMEOUT_MS) != 0) {
		os_event_signal(release_texture_encode_gate);
		obs_leave_graphics();
		pthread_join(stop_thread, NULL);
		pthread_join(start_thread, NULL);
		return RESULT_SETUP_FAILED;
	}
	/* Without the fix, B queues on graphics to build a pool on the empty list,
	 * and A's stale free can then destroy it. The fix keeps B in the teardown
	 * wait instead. These two bounded windows only order B ahead of A on the
	 * graphics lock; whether B returns early is not asserted, and the fixed path
	 * is correct under any interleaving. */
	os_event_timedwait(race.start_done, GPU_STOP_START_RACE_START_WINDOW_MS);
	os_event_signal(release_texture_encode_gate);
	os_sleep_ms(GPU_STOP_START_RACE_A_JOIN_WINDOW_MS);
	obs_leave_graphics();
	if (os_event_timedwait(race.start_done, STEP_TIMEOUT_MS) != 0)
		return RESULT_RACE_START_HUNG;
	pthread_join(start_thread, NULL);
	texture_encode_gate_entered = NULL;
	release_texture_encode_gate = NULL;
	if (!wait_inactive(output_a, STEP_TIMEOUT_MS))
		return RESULT_RACE_A_END_CAPTURE_HUNG;
	if (os_event_timedwait(race.stop_done, STEP_TIMEOUT_MS) != 0)
		return RESULT_RACE_STOP_HUNG;
	pthread_join(stop_thread, NULL);
	if (!race.start_success || !obs_output_active(output_b))
		return RESULT_START_FAILED;
	long encoded_before = os_atomic_load_long(&watched_texture_encodes[0]);
	if (!wait_counter(&watched_texture_encodes[0], encoded_before + 10, STEP_TIMEOUT_MS))
		return RESULT_RACE_FRAME_FAILED;

	/* With the old last-stop path, A can free the pool B started using, and
	 * B's production stop then cannot drain its GPU encoder worker. */
	pthread_t stop_b_thread;
	if (pthread_create(&stop_b_thread, NULL, stop_output_b_thread, &race) != 0)
		return RESULT_SETUP_FAILED;
	if (os_event_timedwait(race.b_stop_done, STEP_TIMEOUT_MS) != 0)
		return RESULT_RACE_B_STOP_HUNG;
	pthread_join(stop_b_thread, NULL);
	if (!wait_inactive(output_b, STEP_TIMEOUT_MS))
		return RESULT_RACE_B_END_CAPTURE_HUNG;
	watched_texture_encoders[0] = NULL;
	/* This child is process-isolated. Exit without teardown so the regression
	 * assertion remains B's frames and bounded stop, not A's cleanup path. */
	return RESULT_OK;
}

/* Fix-specific: while one last stop is tearing the pool down, two starts on
 * the same mix must both be inside the teardown condition wait, and the end of
 * teardown must wake both. A notification that wakes only one leaves the other
 * start blocked (RESULT_BROADCAST_B/C_START_HUNG). Without the fix there is no
 * such wait, and this child exits with RESULT_BROADCAST_NO_START_CONDVAR_WAIT
 * before releasing A; it never runs the stale-pool race, which is
 * last_gpu_encoder_stop_serializes_same_mix_start's job. */
static int gpu_encoder_teardown_wakes_every_waiting_start_child(int unused)
{
	UNUSED_PARAMETER(unused);
	if (!init_core())
		return RESULT_SETUP_FAILED;
	register_encoders();
	register_outputs();

	obs_encoder_t *encoder_a = obs_video_encoder_create(TEX_VENC_ID, "encoder A", NULL, NULL);
	obs_encoder_t *encoder_b = obs_video_encoder_create(TEX_VENC_ID, "encoder B", NULL, NULL);
	obs_encoder_t *encoder_c = obs_video_encoder_create(TEX_VENC_ID, "encoder C", NULL, NULL);
	obs_encoder_t *audio_a = obs_audio_encoder_create(AENC_ID, "audio A", NULL, 0, NULL);
	obs_encoder_t *audio_b = obs_audio_encoder_create(AENC_ID, "audio B", NULL, 0, NULL);
	obs_encoder_t *audio_c = obs_audio_encoder_create(AENC_ID, "audio C", NULL, 0, NULL);
	obs_output_t *output_a = obs_output_create(RECORDING_ID, "output A", NULL, NULL);
	obs_output_t *output_b = obs_output_create(RECORDING_ID, "output B", NULL, NULL);
	obs_output_t *output_c = obs_output_create(RECORDING_ID, "output C", NULL, NULL);
	if (!encoder_a || !encoder_b || !encoder_c || !audio_a || !audio_b || !audio_c || !output_a || !output_b ||
	    !output_c)
		return RESULT_SETUP_FAILED;

	obs_encoder_set_video(encoder_a, obs_get_video());
	obs_encoder_set_video(encoder_b, obs_get_video());
	obs_encoder_set_video(encoder_c, obs_get_video());
	obs_encoder_set_audio(audio_a, obs_get_audio());
	obs_encoder_set_audio(audio_b, obs_get_audio());
	obs_encoder_set_audio(audio_c, obs_get_audio());
	obs_output_set_video_encoder(output_a, encoder_a);
	obs_output_set_video_encoder(output_b, encoder_b);
	obs_output_set_video_encoder(output_c, encoder_c);
	obs_output_set_audio_encoder(output_a, audio_a, 0);
	obs_output_set_audio_encoder(output_b, audio_b, 0);
	obs_output_set_audio_encoder(output_c, audio_c, 0);
	/* Publish these before A starts so its callback never races a pointer write. */
	watched_texture_encoders[0] = encoder_b;
	watched_texture_encoders[1] = encoder_c;

	struct stop_start_race race = {.output_a = output_a, .output_b = output_b, .output_c = output_c};
	if (os_event_init(&race.stop_done, OS_EVENT_TYPE_MANUAL) != 0 ||
	    os_event_init(&race.start_attempted, OS_EVENT_TYPE_MANUAL) != 0 ||
	    os_event_init(&race.c_start_attempted, OS_EVENT_TYPE_MANUAL) != 0 ||
	    os_event_init(&race.start_done, OS_EVENT_TYPE_MANUAL) != 0 ||
	    os_event_init(&race.c_start_done, OS_EVENT_TYPE_MANUAL) != 0 ||
	    os_event_init(&race.b_stop_done, OS_EVENT_TYPE_MANUAL) != 0 ||
	    os_event_init(&race.c_stop_done, OS_EVENT_TYPE_MANUAL) != 0 ||
	    os_event_init(&texture_encode_gate_entered, OS_EVENT_TYPE_MANUAL) != 0 ||
	    os_event_init(&release_texture_encode_gate, OS_EVENT_TYPE_MANUAL) != 0)
		return RESULT_SETUP_FAILED;

	if (!obs_output_start(output_a))
		return RESULT_NO_TEXTURE_PATH;
	if (!wait_counter(&texture_encodes, 3, STEP_TIMEOUT_MS))
		return RESULT_NO_TEXTURE_PATH;
	struct obs_core_video_mix *mix = main_video_mix();
	if (!mix)
		return RESULT_SETUP_FAILED;

	os_atomic_set_bool(&block_next_texture_encode, true);
	os_atomic_set_bool(&fail_blocked_texture_encode, false);
	if (os_event_timedwait(texture_encode_gate_entered, STEP_TIMEOUT_MS) != 0)
		return RESULT_NO_TEXTURE_PATH;

	pthread_t stop_thread;
	if (pthread_create(&stop_thread, NULL, stop_output_thread, &race) != 0)
		return RESULT_SETUP_FAILED;
	if (os_event_timedwait(race.stop_done, STEP_TIMEOUT_MS) != 0)
		return RESULT_SETUP_FAILED;
	/* Start B and C only once A's last stop has removed it, so a correct start
	 * cannot legitimately join A's still-live pool instead of waiting. The
	 * assertion below does not rest on this observation: a start records a wait
	 * only on the mix's teardown condition, which it waits on only while teardown
	 * is marked. */
	if (!wait_for_last_gpu_encoder_removed(mix, STEP_TIMEOUT_MS)) {
		os_event_signal(release_texture_encode_gate);
		pthread_join(stop_thread, NULL);
		return RESULT_LAST_STOP_NOT_REACHED;
	}
	reset_start_wait_observations(mix);

	pthread_t start_b_thread;
	pthread_t start_c_thread;
	if (pthread_create(&start_b_thread, NULL, start_output_thread, &race) != 0)
		return RESULT_SETUP_FAILED;
	if (pthread_create(&start_c_thread, NULL, start_output_c_thread, &race) != 0)
		return RESULT_SETUP_FAILED;

	/* A stays held in its texture callback, so its teardown cannot finish and
	 * neither start can leave the wait. Both must be observed inside the same
	 * condition wait, and the shared mutex must be free: the last start to enter
	 * holds it until its real wait releases it, so a successful trylock proves
	 * both waits were entered. A start that returns without having waited
	 * proves this build has no start-side teardown wait. */
	enum result admission = RESULT_BROADCAST_STARTS_STALLED;
	for (int i = 0; i < STEP_TIMEOUT_MS; i++) {
		long b_waits = os_atomic_load_long(&start_waiters_b);
		long c_waits = os_atomic_load_long(&start_waiters_c);
		bool b_returned = os_atomic_load_long(&race.start_returned_b) != 0;
		bool c_returned = os_atomic_load_long(&race.start_returned_c) != 0;
		if ((b_returned && !b_waits) || (c_returned && !c_waits)) {
			admission = RESULT_BROADCAST_NO_START_CONDVAR_WAIT;
			break;
		}
		if (b_waits && c_waits) {
			bool same_wait = start_wait_cond_b == start_wait_cond_c &&
					 start_wait_mutex_b == start_wait_mutex_c;
			bool both_waiting = same_wait && wait_for_mutex_release(start_wait_mutex_b, STEP_TIMEOUT_MS) &&
					    os_atomic_load_long(&race.start_returned_b) == 0 &&
					    os_atomic_load_long(&race.start_returned_c) == 0;
			admission = both_waiting ? RESULT_OK : RESULT_BROADCAST_WAITERS_MISSING;
			break;
		}
		os_sleep_ms(1);
	}
	if (admission == RESULT_BROADCAST_STARTS_STALLED &&
	    (os_atomic_load_long(&start_waiters_b) || os_atomic_load_long(&start_waiters_c)))
		admission = RESULT_BROADCAST_WAITERS_MISSING;
	/* On failure, exit with A still held. Releasing it here would let a build
	 * without the wait run the stale-pool race and report that instead. */
	if (admission != RESULT_OK)
		return admission;

	os_event_signal(release_texture_encode_gate);
	if (!wait_inactive(output_a, STEP_TIMEOUT_MS))
		return RESULT_BROADCAST_A_END_CAPTURE_HUNG;
	pthread_join(stop_thread, NULL);
	if (os_event_timedwait(race.start_done, STEP_TIMEOUT_MS) != 0)
		return RESULT_BROADCAST_B_START_HUNG;
	if (os_event_timedwait(race.c_start_done, STEP_TIMEOUT_MS) != 0)
		return RESULT_BROADCAST_C_START_HUNG;
	pthread_join(start_b_thread, NULL);
	pthread_join(start_c_thread, NULL);
	if (!race.start_success || !race.c_start_success || !obs_output_active(output_b) ||
	    !obs_output_active(output_c))
		return RESULT_BROADCAST_START_FAILED;

	long b_encoded_before = os_atomic_load_long(&watched_texture_encodes[0]);
	long c_encoded_before = os_atomic_load_long(&watched_texture_encodes[1]);
	if (!wait_counter(&watched_texture_encodes[0], b_encoded_before + 10, STEP_TIMEOUT_MS))
		return RESULT_BROADCAST_B_FRAME_FAILED;
	if (!wait_counter(&watched_texture_encodes[1], c_encoded_before + 10, STEP_TIMEOUT_MS))
		return RESULT_BROADCAST_C_FRAME_FAILED;

	/* Bound both real asynchronous teardowns, including C's last-encoder pool
	 * drain. The API stop call returning is not the completion condition. */
	pthread_t stop_b_thread;
	if (pthread_create(&stop_b_thread, NULL, stop_output_b_thread, &race) != 0)
		return RESULT_SETUP_FAILED;
	if (os_event_timedwait(race.b_stop_done, STEP_TIMEOUT_MS) != 0 || !wait_inactive(output_b, STEP_TIMEOUT_MS))
		return RESULT_BROADCAST_B_STOP_HUNG;
	pthread_join(stop_b_thread, NULL);

	pthread_t stop_c_thread;
	if (pthread_create(&stop_c_thread, NULL, stop_output_c_thread, &race) != 0)
		return RESULT_SETUP_FAILED;
	if (os_event_timedwait(race.c_stop_done, STEP_TIMEOUT_MS) != 0 || !wait_inactive(output_c, STEP_TIMEOUT_MS))
		return RESULT_BROADCAST_C_STOP_HUNG;
	pthread_join(stop_c_thread, NULL);

	fprintf(stderr,
		"GPU encode teardown broadcast: B and C were both in the teardown wait (%ld, %ld waits), "
		"then both restarted and encoded frames\n",
		os_atomic_load_long(&start_waiters_b), os_atomic_load_long(&start_waiters_c));
	return RESULT_OK;
}

static int gpu_stop_keeps_mix_active_child(int unused)
{
	UNUSED_PARAMETER(unused);
	if (!init_core())
		return RESULT_SETUP_FAILED;
	register_encoders();
	register_outputs();

	obs_encoder_t *encoder = obs_video_encoder_create(TEX_VENC_ID, "encoder", NULL, NULL);
	obs_encoder_t *audio_encoder = obs_audio_encoder_create(AENC_ID, "audio", NULL, 0, NULL);
	obs_output_t *output = obs_output_create(RECORDING_ID, "output", NULL, NULL);
	if (!encoder || !audio_encoder || !output)
		return RESULT_SETUP_FAILED;

	obs_encoder_set_video(encoder, obs_get_video());
	obs_encoder_set_audio(audio_encoder, obs_get_audio());
	obs_output_set_video_encoder(output, encoder);
	obs_output_set_audio_encoder(output, audio_encoder, 0);
	if (!obs_output_start(output))
		return RESULT_NO_TEXTURE_PATH;
	if (!wait_counter(&texture_encodes, 3, STEP_TIMEOUT_MS))
		return RESULT_NO_TEXTURE_PATH;

	struct stop_start_race race = {.output_a = output};
	if (os_event_init(&race.stop_done, OS_EVENT_TYPE_MANUAL) != 0 ||
	    os_event_init(&texture_encode_gate_entered, OS_EVENT_TYPE_MANUAL) != 0 ||
	    os_event_init(&release_texture_encode_gate, OS_EVENT_TYPE_MANUAL) != 0)
		return RESULT_SETUP_FAILED;

	os_atomic_set_bool(&block_next_texture_encode, true);
	os_atomic_set_bool(&fail_blocked_texture_encode, false);
	if (os_event_timedwait(texture_encode_gate_entered, STEP_TIMEOUT_MS) != 0)
		return RESULT_NO_TEXTURE_PATH;

	struct obs_core_video_mix *mix = main_video_mix();
	if (!mix)
		return RESULT_SETUP_FAILED;
	pthread_t stop_thread;
	if (pthread_create(&stop_thread, NULL, stop_output_thread, &race) != 0)
		return RESULT_SETUP_FAILED;
	/* Sample once the last stop has removed the encoder and is waiting for the
	 * worker, which the held callback blocks: obs_reset_video() must still see
	 * the mix as active there. */
	if (!wait_for_last_gpu_encoder_removed(mix, STEP_TIMEOUT_MS)) {
		os_event_signal(release_texture_encode_gate);
		pthread_join(stop_thread, NULL);
		return RESULT_LAST_STOP_NOT_REACHED;
	}
	/* Report an inactive mix at once: the child is process-isolated, and the
	 * old stop's teardown that follows is not what this case checks. */
	if (!obs_video_active())
		return RESULT_MIX_INACTIVE_DURING_DRAIN;
	os_event_signal(release_texture_encode_gate);
	if (os_event_timedwait(race.stop_done, STEP_TIMEOUT_MS) != 0) {
		pthread_join(stop_thread, NULL);
		return RESULT_RACE_STOP_HUNG;
	}
	pthread_join(stop_thread, NULL);
	texture_encode_gate_entered = NULL;
	release_texture_encode_gate = NULL;

	obs_output_release(output);
	obs_encoder_release(encoder);
	obs_encoder_release(audio_encoder);
	obs_shutdown();
	return RESULT_OK;
}

static int gpu_error_during_stop_child(int unused)
{
	UNUSED_PARAMETER(unused);
	if (!init_core())
		return RESULT_SETUP_FAILED;
	register_encoders();
	register_outputs();

	obs_encoder_t *encoder_b = obs_video_encoder_create(TEX_VENC_ID, "encoder B", NULL, NULL);
	obs_encoder_t *audio_encoder = obs_audio_encoder_create(AENC_ID, "audio", NULL, 0, NULL);
	obs_output_t *output_a = obs_output_create(RECORDING_ID, "output A", NULL, NULL);
	if (!encoder_b || !audio_encoder || !output_a)
		return RESULT_SETUP_FAILED;

	obs_encoder_set_video(encoder_b, obs_get_video());
	obs_encoder_set_audio(audio_encoder, obs_get_audio());
	obs_output_set_video_encoder(output_a, encoder_b);
	obs_output_set_audio_encoder(output_a, audio_encoder, 0);

	struct stop_start_race race = {.output_a = output_a};
	if (os_event_init(&race.stop_done, OS_EVENT_TYPE_MANUAL) != 0 ||
	    os_event_init(&texture_encode_gate_entered, OS_EVENT_TYPE_MANUAL) != 0 ||
	    os_event_init(&release_texture_encode_gate, OS_EVENT_TYPE_MANUAL) != 0)
		return RESULT_SETUP_FAILED;

	os_atomic_set_bool(&block_next_texture_encode, true);
	os_atomic_set_bool(&fail_blocked_texture_encode, true);
	if (!obs_output_start(output_a))
		return RESULT_NO_TEXTURE_PATH;
	if (os_event_timedwait(texture_encode_gate_entered, STEP_TIMEOUT_MS) != 0)
		return RESULT_NO_TEXTURE_PATH;

	pthread_t stop_thread;
	if (pthread_create(&stop_thread, NULL, stop_output_thread, &race) != 0)
		return RESULT_SETUP_FAILED;
	/* The callback is still in flight while A's last stop reaches the worker
	 * drain. This check must not rely on obs_video_active(), which includes the
	 * teardown lifetime pin on the fixed path. */
	os_sleep_ms(100);
	if (!obs_output_active(output_a)) {
		os_event_signal(release_texture_encode_gate);
		pthread_join(stop_thread, NULL);
		return RESULT_STOP_FINISHED_EARLY;
	}

	os_event_signal(release_texture_encode_gate);
	if (!wait_inactive(output_a, STEP_TIMEOUT_MS)) {
		pthread_join(stop_thread, NULL);
		return RESULT_GPU_ERROR_STOP_HUNG;
	}
	if (os_event_timedwait(race.stop_done, STEP_TIMEOUT_MS) != 0) {
		pthread_join(stop_thread, NULL);
		return RESULT_GPU_ERROR_STOP_HUNG;
	}
	pthread_join(stop_thread, NULL);
	texture_encode_gate_entered = NULL;
	release_texture_encode_gate = NULL;

	obs_output_release(output_a);
	obs_encoder_release(encoder_b);
	obs_encoder_release(audio_encoder);
	obs_shutdown();
	return RESULT_OK;
}

static int scenario_child(int scenario)
{
	/* a failed scenario returns without tearing down, which may hang */
	return (int)run_scenario((enum scenario)scenario);
}

static int probe_child(int unused)
{
	UNUSED_PARAMETER(unused);
	if (!init_core())
		return 1;
	obs_shutdown();
	return 0;
}

static void expect_ok(enum scenario scenario, const char *what)
{
	int r = run_in_child(scenario_child, (int)scenario);
	if (r == RESULT_NO_TEXTURE_PATH) {
		fprintf(stderr, "%s: the core video mix has no texture encoding path, skipping\n", what);
		skip();
	}
	if (r == -1) {
		fprintf(stderr, "%s: fork failed\n", what);
		fail();
	}
	if (r == -2) {
		fprintf(stderr, "%s: did not finish within %d ms\n", what, CHILD_TIMEOUT_MS);
		fail();
	}
	if (r <= -100) {
		fprintf(stderr, "%s: crashed with signal %d\n", what, -100 - r);
		fail();
	}
	if (r != RESULT_OK) {
		fprintf(stderr, "%s: %s (result %d)\n", what, result_name(r), r);
		fail();
	}
}
#endif

/* The reporter's case: a replay buffer that is set up on the recording's
 * encoders but not running must start after the recording's encoder failed. */
static void raw_idle_replay_buffer_starts_after_error(void **state)
{
	UNUSED_PARAMETER(state);
	if (!obs_up)
		skip();
#ifndef SCENARIO_UNSUPPORTED
	expect_ok(SCENARIO_RAW_IDLE_REPLAY_BUFFER, "raw encoder error, idle replay buffer");
#endif
}

/* Control: a replay buffer that was running when the encoder failed stops
 * with the recording and starts again. */
static void raw_active_replay_buffer_restarts_after_error(void **state)
{
	UNUSED_PARAMETER(state);
	if (!obs_up)
		skip();
#ifndef SCENARIO_UNSUPPORTED
	expect_ok(SCENARIO_RAW_ACTIVE_REPLAY_BUFFER, "raw encoder error, running replay buffer");
#endif
}

/* A texture encoder failing must not deadlock the GPU encode thread: the
 * recording stops, and starts and encodes again. */
static void texture_error_does_not_deadlock(void **state)
{
	UNUSED_PARAMETER(state);
	if (!obs_up)
		skip();
#ifndef SCENARIO_UNSUPPORTED
	expect_ok(SCENARIO_TEXTURE_RECORDING, "texture encoder error");
#endif
}

/* Both at once: texture encoder error with an idle replay buffer. */
static void texture_error_idle_replay_buffer_starts(void **state)
{
	UNUSED_PARAMETER(state);
	if (!obs_up)
		skip();
#ifndef SCENARIO_UNSUPPORTED
	expect_ok(SCENARIO_TEXTURE_IDLE_REPLAY_BUFFER, "texture encoder error, idle replay buffer");
#endif
}

/* Original bug reproducer, independent of how the fix works. A's last stop is
 * held in its worker drain while B starts on the same mix. Without the fix B
 * builds a new pool that A's stale teardown then frees, so B gets no frames or
 * cannot stop (or the process crashes on the freed pool). Each cycle is a
 * separate process. */
static void last_gpu_encoder_stop_serializes_same_mix_start(void **state)
{
	UNUSED_PARAMETER(state);
	if (!obs_up)
		skip();
#ifndef SCENARIO_UNSUPPORTED
	uint64_t end_time = monotonic_time_ms() + GPU_STOP_START_RACE_TOTAL_TIMEOUT_MS;
	int completed = 0;
	for (int cycle = 0; cycle < GPU_STOP_START_RACE_ATTEMPTS; cycle++) {
		uint64_t now = monotonic_time_ms();
		if (now >= end_time)
			break;
		int remaining_ms = (int)(end_time - now);
		int timeout_ms = remaining_ms < GPU_STOP_START_RACE_CHILD_TIMEOUT_MS
					 ? remaining_ms
					 : GPU_STOP_START_RACE_CHILD_TIMEOUT_MS;
		int r = run_in_child_timeout(stop_start_race_child, cycle, timeout_ms);
		if (r == RESULT_OK) {
			completed++;
			continue;
		}
		if (r == RESULT_NO_TEXTURE_PATH) {
			fprintf(stderr, "GPU encode stop/start race: no texture encoding path, skipping\n");
			skip();
		}
		if (r == -1) {
			fprintf(stderr, "GPU encode stop/start race: fork failed\n");
			fail();
		}
		if (r == -2) {
			fprintf(stderr, "GPU encode stop/start race: cycle %d/%d timed out after A's stale teardown\n",
				completed + 1, GPU_STOP_START_RACE_ATTEMPTS);
			fail();
		}
		if (r <= -100) {
			fprintf(stderr,
				"GPU encode stop/start race: cycle %d/%d crashed with signal %d after A's stale teardown\n",
				completed + 1, GPU_STOP_START_RACE_ATTEMPTS, -100 - r);
			fail();
		}
		fprintf(stderr, "GPU encode stop/start race: cycle %d/%d failed: %s (result %d)\n", completed + 1,
			GPU_STOP_START_RACE_ATTEMPTS, result_name(r), r);
		fail();
	}
	if (completed != GPU_STOP_START_RACE_ATTEMPTS) {
		fprintf(stderr, "GPU encode stop/start race: bounded out after %d/%d cycles\n", completed,
			GPU_STOP_START_RACE_ATTEMPTS);
		fail();
	}
	fprintf(stderr, "GPU encode stop/start race: B kept its pool, encoded and stopped in %d/%d cycles\n", completed,
		GPU_STOP_START_RACE_ATTEMPTS);
#endif
}

/* Specific to the fix (see the child). This is not a race reproducer: without
 * the fix it fails with RESULT_BROADCAST_NO_START_CONDVAR_WAIT. */
static void gpu_encoder_teardown_wakes_every_waiting_start(void **state)
{
	UNUSED_PARAMETER(state);
	if (!obs_up)
		skip();
#ifndef SCENARIO_UNSUPPORTED
	int r = run_in_child_timeout(gpu_encoder_teardown_wakes_every_waiting_start_child, 0,
				     GPU_STOP_START_RACE_CHILD_TIMEOUT_MS);
	if (r == RESULT_NO_TEXTURE_PATH) {
		fprintf(stderr, "GPU encode teardown broadcast: no texture encoding path, skipping\n");
		skip();
	}
	if (r == -1) {
		fprintf(stderr, "GPU encode teardown broadcast: fork failed\n");
		fail();
	}
	if (r == -2) {
		fprintf(stderr, "GPU encode teardown broadcast: child timed out within %d ms\n",
			GPU_STOP_START_RACE_CHILD_TIMEOUT_MS);
		fail();
	}
	if (r <= -100) {
		fprintf(stderr, "GPU encode teardown broadcast: child crashed with signal %d\n", -100 - r);
		fail();
	}
	if (r != RESULT_OK) {
		fprintf(stderr, "GPU encode teardown broadcast: %s (result %d)\n", result_name(r), r);
		fail();
	}
#endif
}

static void gpu_encoder_stop_keeps_mix_active_during_drain(void **state)
{
	UNUSED_PARAMETER(state);
	if (!obs_up)
		skip();
#ifndef SCENARIO_UNSUPPORTED
	int r = run_in_child(gpu_stop_keeps_mix_active_child, 0);
	if (r == RESULT_NO_TEXTURE_PATH) {
		fprintf(stderr, "GPU encoder mix lifetime: no texture encoding path, skipping\n");
		skip();
	}
	if (r == -1) {
		fprintf(stderr, "GPU encoder mix lifetime: fork failed\n");
		fail();
	}
	if (r == -2) {
		fprintf(stderr, "GPU encoder mix lifetime: did not finish within %d ms\n", CHILD_TIMEOUT_MS);
		fail();
	}
	if (r <= -100) {
		fprintf(stderr, "GPU encoder mix lifetime: crashed with signal %d\n", -100 - r);
		fail();
	}
	if (r != RESULT_OK) {
		fprintf(stderr, "GPU encoder mix lifetime: %s (result %d)\n", result_name(r), r);
		fail();
	}
#endif
}

static void gpu_encoder_failure_during_last_stop_finishes(void **state)
{
	UNUSED_PARAMETER(state);
	if (!obs_up)
		skip();
#ifndef SCENARIO_UNSUPPORTED
	int r = run_in_child(gpu_error_during_stop_child, 0);
	if (r == RESULT_NO_TEXTURE_PATH) {
		fprintf(stderr, "GPU encode failure during stop: no texture encoding path, skipping\n");
		skip();
	}
	if (r == -1) {
		fprintf(stderr, "GPU encode failure during stop: fork failed\n");
		fail();
	}
	if (r == -2) {
		fprintf(stderr, "GPU encode failure during stop: did not finish within %d ms\n", CHILD_TIMEOUT_MS);
		fail();
	}
	if (r <= -100) {
		fprintf(stderr, "GPU encode failure during stop: crashed with signal %d\n", -100 - r);
		fail();
	}
	if (r != RESULT_OK) {
		fprintf(stderr, "GPU encode failure during stop: %s (result %d)\n", result_name(r), r);
		fail();
	}
#endif
}

/* libobs is started in a child process per scenario. Probe once that it can
 * start here at all (it needs a display and a graphics module). */
static int group_setup(void **state)
{
	UNUSED_PARAMETER(state);
#ifdef SCENARIO_UNSUPPORTED
	obs_up = 0;
#else
	const char *display = getenv("DISPLAY");
	obs_up = display && *display && run_in_child(probe_child, 0) == 0;
	if (!obs_up)
		fprintf(stderr, "libobs core video is not available here (no display?), skipping\n");
#endif
	return 0;
}

int main(void)
{
	const struct CMUnitTest tests[] = {
		cmocka_unit_test(raw_active_replay_buffer_restarts_after_error),
		cmocka_unit_test(raw_idle_replay_buffer_starts_after_error),
		cmocka_unit_test(texture_error_does_not_deadlock),
		cmocka_unit_test(texture_error_idle_replay_buffer_starts),
		cmocka_unit_test(last_gpu_encoder_stop_serializes_same_mix_start),
		cmocka_unit_test(gpu_encoder_teardown_wakes_every_waiting_start),
		cmocka_unit_test(gpu_encoder_stop_keeps_mix_active_during_drain),
		cmocka_unit_test(gpu_encoder_failure_during_last_stop_finishes),
	};
	return cmocka_run_group_tests(tests, group_setup, NULL);
}
