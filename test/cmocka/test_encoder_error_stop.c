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
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
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
#define PREFLIGHT_FALLBACK_ID "test_err_async_preflight_fallback_encoder"
#define ASYNC_ERROR_ID "test_err_async_route_loss_encoder"
#define AENC_ID "test_err_audio_encoder"
#define RECORDING_ID "test_err_recording"

/* How long a start or stop may take. A healthy one takes a few frames. */
#define STEP_TIMEOUT_MS 5000
/* Watchdog for the whole child, in case a step blocks the calling thread. */
#define CHILD_TIMEOUT_MS 40000
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
	default:
		return "unknown";
	}
}

/* ------------------------------------------------------------------------- */
/* encoders */

static volatile long video_packets;
static volatile long texture_encodes;
static volatile long async_sample_probes;
static volatile bool async_route_available;
static volatile long failures;
static volatile long venc_creates;
static volatile long venc_destroys;
/* encode calls left until the one that fails, -1 when not armed */
static volatile long fail_in = -1;
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
	os_atomic_inc_long(&texture_encodes);
	if (encode_should_fail())
		return false;
	make_video_packet(packet, pts, received);
	return true;
}

static bool async_preflight_encode(void *data, struct encoder_texture *texture, int64_t pts, uint64_t lock_key,
				   uint64_t *next_key, struct encoder_packet *packet, bool *received)
{
	UNUSED_PARAMETER(data);
	UNUSED_PARAMETER(texture);
	UNUSED_PARAMETER(pts);
	UNUSED_PARAMETER(lock_key);
	UNUSED_PARAMETER(next_key);
	UNUSED_PARAMETER(packet);
	UNUSED_PARAMETER(received);
	return true;
}

static bool async_preflight_can_encode(void *data, const struct encoder_texture *texture)
{
	UNUSED_PARAMETER(data);
	if (!texture)
		return true;

	/* The route probe succeeds, but the real sample probe rejects the route. */
	os_atomic_inc_long(&async_sample_probes);
	return false;
}

static void async_preflight_drain(void *data)
{
	UNUSED_PARAMETER(data);
}

static bool async_error_encode(void *data, struct encoder_texture *texture, int64_t pts, uint64_t lock_key,
			       uint64_t *next_key, struct encoder_packet *packet, bool *received)
{
	UNUSED_PARAMETER(data);
	UNUSED_PARAMETER(texture);
	UNUSED_PARAMETER(lock_key);
	UNUSED_PARAMETER(next_key);
	os_atomic_inc_long(&texture_encodes);
	if (encode_should_fail()) {
		os_atomic_set_bool(&async_route_available, false);
		return false;
	}
	make_video_packet(packet, pts, received);
	return true;
}

static bool async_error_can_encode(void *data, const struct encoder_texture *texture)
{
	UNUSED_PARAMETER(data);
	UNUSED_PARAMETER(texture);
	return os_atomic_load_bool(&async_route_available);
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

	struct obs_encoder_info async_preflight = {
		.id = PREFLIGHT_FALLBACK_ID,
		.type = OBS_ENCODER_VIDEO,
		.codec = "h264",
		.get_name = enc_name,
		.create = venc_create,
		.destroy = venc_destroy,
		.encode = raw_venc_encode,
		.encode_texture2 = async_preflight_encode,
		.can_encode_texture = async_preflight_can_encode,
		.drain_texture_encode = async_preflight_drain,
		.caps = OBS_ENCODER_CAP_PASS_TEXTURE | OBS_ENCODER_CAP_ASYNC_TEXTURE,
	};
	obs_register_encoder(&async_preflight);

	struct obs_encoder_info async_error = {
		.id = ASYNC_ERROR_ID,
		.type = OBS_ENCODER_VIDEO,
		.codec = "h264",
		.get_name = enc_name,
		.create = venc_create,
		.destroy = venc_destroy,
		.encode = raw_venc_encode,
		.encode_texture2 = async_error_encode,
		.can_encode_texture = async_error_can_encode,
		.drain_texture_encode = async_preflight_drain,
		.caps = OBS_ENCODER_CAP_PASS_TEXTURE | OBS_ENCODER_CAP_ASYNC_TEXTURE,
	};
	obs_register_encoder(&async_error);

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
	/* async route probe succeeds, sample probe rejects, then raw fallback
	 * stops and restarts after the GPU worker is joined */
	SCENARIO_ASYNC_PREFLIGHT_FALLBACK,
	/* async texture encode fails and capability disappears before full_stop */
	SCENARIO_ASYNC_ERROR_ROUTE_LOST,
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
	bool async_preflight = scenario == SCENARIO_ASYNC_PREFLIGHT_FALLBACK;
	bool async_error = scenario == SCENARIO_ASYNC_ERROR_ROUTE_LOST;
	texture = texture || async_error;
	bool with_rb = scenario != SCENARIO_TEXTURE_RECORDING && !async_preflight && !async_error;
	bool rb_running = scenario == SCENARIO_RAW_ACTIVE_REPLAY_BUFFER;
	enum result r;

	if (!init_core())
		return RESULT_SETUP_FAILED;
	if (async_error)
		os_atomic_set_bool(&async_route_available, true);
	register_encoders();
	register_outputs();

	const char *venc_id = async_preflight ? PREFLIGHT_FALLBACK_ID
			      : async_error   ? ASYNC_ERROR_ID
			      : texture       ? TEX_VENC_ID
					      : RAW_VENC_ID;
	obs_encoder_t *venc = obs_video_encoder_create(venc_id, "venc", NULL, NULL);
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
	if (async_preflight && !os_atomic_load_long(&async_sample_probes))
		return RESULT_NO_PACKETS;

	if (async_preflight) {
		r = stop_with_timeout(rec);
		if (r != RESULT_OK)
			return r;

		long restart_target = os_atomic_load_long(&video_packets) + 15;
		r = start_with_timeout(rec);
		if (r != RESULT_OK)
			return r;
		if (!wait_counter(&video_packets, restart_target, STEP_TIMEOUT_MS))
			return RESULT_NOT_ENCODING;
		if (os_atomic_load_long(&async_sample_probes) < 2)
			return RESULT_NO_PACKETS;
		r = stop_with_timeout(rec);
		if (r != RESULT_OK)
			return r;
	} else {
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
	}

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

static int run_in_child(int (*fn)(int), int arg)
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
	for (int i = 0; i < CHILD_TIMEOUT_MS / 10; i++) {
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

static void async_sample_probe_falls_back_and_restarts(void **state)
{
	UNUSED_PARAMETER(state);
	if (!obs_up)
		skip();
#ifndef SCENARIO_UNSUPPORTED
	expect_ok(SCENARIO_ASYNC_PREFLIGHT_FALLBACK, "async texture sample probe fallback and restart");
#endif
}

static void async_error_uses_committed_texture_route(void **state)
{
	UNUSED_PARAMETER(state);
	if (!obs_up)
		skip();
#ifndef SCENARIO_UNSUPPORTED
	expect_ok(SCENARIO_ASYNC_ERROR_ROUTE_LOST, "async encoder error uses committed texture route");
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
		cmocka_unit_test(async_sample_probe_falls_back_and_restarts),
		cmocka_unit_test(async_error_uses_committed_texture_route),
	};
	return cmocka_run_group_tests(tests, group_setup, NULL);
}
