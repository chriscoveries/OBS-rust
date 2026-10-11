/* Regression test for upstream obsproject/obs-studio#12731 (reported by
 * tomben13): starting an output with a texture-only video encoder
 * while the video format has no GPU texture path (anything but NV12/P010)
 * crashed on the video thread.
 *
 * libobs only feeds an encoder textures when the encoder advertises
 * OBS_ENCODER_CAP_PASS_TEXTURE *and* the video mix produces NV12/P010
 * textures. Otherwise it connects the encoder to raw frames and do_encode()
 * calls info.encode, which a texture-only encoder (encode == NULL,
 * encode_texture2 set) does not have: a call through a NULL pointer.
 *
 * Texture encoders are expected to reroute to their CPU sibling from
 * create() when textures are unavailable (as obs-nvenc and obs-qsv11 do).
 * When an encoder does not, libobs must refuse to start the output with an
 * error instead of crashing.
 *
 * The scenario needs no GPU: a bare video_t (no core video mix) always
 * takes the raw-frame path. obs_startup needs a display on Linux, so the
 * tests skip without one (run under xvfb-run).
 *
 * Starting an encoded output reads the core video's frame rate through
 * obs_get_video_info() (calculate_batch_size() in obs-output.c) and ignores
 * its return value. Without a core video mix that is an uninitialized
 * fps_num, which divides by zero (SIGFPE) or not depending on stack garbage.
 * This test therefore supplies the core video info a running OBS would have,
 * by defining obs_get_video_info() in the executable: on ELF platforms it
 * interposes libobs's own definition. Mach-O's two-level namespace does not
 * allow that, so the tests skip on macOS as well as Windows.
 */
#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <cmocka.h>

#include <stdio.h>
#include <stdlib.h>

#ifndef _WIN32
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

#include <obs.h>
#include <media-io/video-io.h>
#include <media-io/video-frame.h>
#include <util/platform.h>
#include <util/threading.h>

#if defined(_WIN32) || defined(__APPLE__)
#define SCENARIO_UNSUPPORTED 1
#endif

#define FPS_NUM 30
#define FPS_DEN 1
#define WIDTH 64
#define HEIGHT 64

#define TEX_ONLY_ID "test_tex_only_encoder"
#define HYBRID_ID "test_hybrid_encoder"
#define OUTPUT_ID "test_encoded_output"

static int obs_up;
static volatile long encode_calls;

/* ------------------------------------------------------------------------- */
/* core video info, see the comment at the top */

bool obs_get_video_info(struct obs_video_info *ovi)
{
	*ovi = (struct obs_video_info){
		.graphics_module = "libobs-opengl",
		.fps_num = FPS_NUM,
		.fps_den = FPS_DEN,
		.base_width = WIDTH,
		.base_height = HEIGHT,
		.output_width = WIDTH,
		.output_height = HEIGHT,
		.output_format = VIDEO_FORMAT_NV12,
		.colorspace = VIDEO_CS_709,
		.range = VIDEO_RANGE_PARTIAL,
		.scale_type = OBS_SCALE_BICUBIC,
	};
	return true;
}

/* ------------------------------------------------------------------------- */
/* fake encoders */

static const char *enc_name(void *type_data)
{
	(void)type_data;
	return "test encoder";
}

static void *enc_create(obs_data_t *settings, obs_encoder_t *encoder)
{
	(void)settings;
	return encoder;
}

static void enc_destroy(void *data)
{
	(void)data;
}

static bool enc_encode(void *data, struct encoder_frame *frame, struct encoder_packet *packet, bool *received_packet)
{
	(void)data;
	(void)frame;
	(void)packet;
	os_atomic_inc_long(&encode_calls);
	*received_packet = false;
	return true;
}

static bool enc_encode_texture2(void *data, struct encoder_texture *texture, int64_t pts, uint64_t lock_key,
				uint64_t *next_key, struct encoder_packet *packet, bool *received_packet)
{
	(void)data;
	(void)texture;
	(void)pts;
	(void)lock_key;
	(void)next_key;
	(void)packet;
	*received_packet = false;
	return true;
}

static void register_encoders(void)
{
	struct obs_encoder_info tex_only = {
		.id = TEX_ONLY_ID,
		.type = OBS_ENCODER_VIDEO,
		.codec = "h264",
		.get_name = enc_name,
		.create = enc_create,
		.destroy = enc_destroy,
		.encode_texture2 = enc_encode_texture2,
		.caps = OBS_ENCODER_CAP_PASS_TEXTURE,
	};
	obs_register_encoder(&tex_only);

	struct obs_encoder_info hybrid = tex_only;
	hybrid.id = HYBRID_ID;
	hybrid.encode = enc_encode;
	obs_register_encoder(&hybrid);
}

/* ------------------------------------------------------------------------- */
/* fake encoded output, started the way real outputs start */

static const char *out_name(void *type_data)
{
	(void)type_data;
	return "test output";
}

static void *out_create(obs_data_t *settings, obs_output_t *output)
{
	(void)settings;
	return output;
}

static void out_destroy(void *data)
{
	(void)data;
}

static bool out_start(void *data)
{
	obs_output_t *output = data;
	if (!obs_output_can_begin_data_capture(output, 0))
		return false;
	if (!obs_output_initialize_encoders(output, 0))
		return false;
	return obs_output_begin_data_capture(output, 0);
}

static void out_stop(void *data, uint64_t ts)
{
	(void)ts;
	obs_output_end_data_capture(data);
}

static void out_packet(void *data, struct encoder_packet *packet)
{
	(void)data;
	(void)packet;
}

static void register_output(void)
{
	struct obs_output_info info = {
		.id = OUTPUT_ID,
		.flags = OBS_OUTPUT_VIDEO | OBS_OUTPUT_ENCODED,
		.get_name = out_name,
		.create = out_create,
		.destroy = out_destroy,
		.start = out_start,
		.stop = out_stop,
		.encoded_packet = out_packet,
	};
	obs_register_output(&info);
}

/* ------------------------------------------------------------------------- */

enum run_result {
	RUN_REFUSED_WITH_ERROR = 0,
	RUN_STARTED_AND_ENCODED = 1,
	RUN_REFUSED_WITHOUT_ERROR = 2,
	RUN_SETUP_FAILED = 3,
	RUN_STARTED_NOTHING_ENCODED = 4,
};

/* Start an output on a raw video_t of the given format, push a few frames
 * through the video thread, stop. Returns how it went. */
static enum run_result run_scenario(const char *encoder_id, enum video_format format)
{
	struct video_output_info voi = {
		.name = "test video",
		.format = format,
		.fps_num = FPS_NUM,
		.fps_den = FPS_DEN,
		.width = WIDTH,
		.height = HEIGHT,
		.range = VIDEO_RANGE_PARTIAL,
		.colorspace = VIDEO_CS_709,
		.cache_size = 4,
	};
	video_t *video = NULL;
	if (video_output_open(&video, &voi) != VIDEO_OUTPUT_SUCCESS)
		return RUN_SETUP_FAILED;

	obs_encoder_t *enc = obs_video_encoder_create(encoder_id, "venc", NULL, NULL);
	obs_output_t *out = obs_output_create(OUTPUT_ID, "out", NULL, NULL);
	if (!enc || !out)
		return RUN_SETUP_FAILED;
	obs_encoder_set_video(enc, video);
	obs_output_set_media(out, video, NULL);
	obs_output_set_video_encoder(out, enc);

	enum run_result result;
	if (!obs_output_start(out)) {
		const char *err = obs_output_get_last_error(out);
		result = (err && *err) ? RUN_REFUSED_WITH_ERROR : RUN_REFUSED_WITHOUT_ERROR;
		if (err)
			fprintf(stderr, "output refused to start: %s\n", err);
	} else {
		uint64_t ts = os_gettime_ns();
		for (int i = 0; i < 8; i++) {
			struct video_frame frame;
			ts += 33333333;
			if (video_output_lock_frame(video, &frame, 1, ts))
				video_output_unlock_frame(video);
			os_sleep_ms(40);
		}
		obs_output_force_stop(out);
		for (int i = 0; i < 100 && obs_output_active(out); i++)
			os_sleep_ms(10);
		result = os_atomic_load_long(&encode_calls) > 0 ? RUN_STARTED_AND_ENCODED : RUN_STARTED_NOTHING_ENCODED;
	}

	obs_output_release(out);
	obs_encoder_release(enc);
	video_output_close(video);
	return result;
}

#ifndef SCENARIO_UNSUPPORTED
/* Run the scenario in a child so a crash fails the test instead of the run. */
static int run_in_child(const char *encoder_id, enum video_format format)
{
	fflush(stderr);
	pid_t pid = fork();
	if (pid == 0) {
		/* cmocka traps fatal signals; let a crash kill the child */
		signal(SIGSEGV, SIG_DFL);
		signal(SIGBUS, SIG_DFL);
		signal(SIGILL, SIG_DFL);
		signal(SIGFPE, SIG_DFL);
		_exit((int)run_scenario(encoder_id, format));
	}

	int status = 0;
	if (waitpid(pid, &status, 0) != pid) {
		fprintf(stderr, "waitpid failed\n");
		fail();
	}
	if (WIFSIGNALED(status)) {
		fprintf(stderr, "%s on %s crashed with signal %d\n", encoder_id, get_video_format_name(format),
			WTERMSIG(status));
		fail();
	}
	return WEXITSTATUS(status);
}
#endif

static void tex_only_on_format(enum video_format format)
{
#ifdef SCENARIO_UNSUPPORTED
	(void)format;
	skip();
#else
	if (!obs_up)
		skip();
	int r = run_in_child(TEX_ONLY_ID, format);
	if (r != RUN_REFUSED_WITH_ERROR) {
		fprintf(stderr,
			"texture-only encoder on %s: expected the output to refuse to start with an error, got %d\n",
			get_video_format_name(format), r);
		fail();
	}
#endif
}

static void tex_only_i444_refused(void **state)
{
	(void)state;
	tex_only_on_format(VIDEO_FORMAT_I444);
}

static void tex_only_i420_refused(void **state)
{
	(void)state;
	tex_only_on_format(VIDEO_FORMAT_I420);
}

static void tex_only_bgra_refused(void **state)
{
	(void)state;
	tex_only_on_format(VIDEO_FORMAT_BGRA);
}

/* Without a core video mix there are no NV12 textures either, so a
 * texture-only encoder cannot run on NV12 raw frames: same refusal. */
static void tex_only_nv12_without_textures_refused(void **state)
{
	(void)state;
	tex_only_on_format(VIDEO_FORMAT_NV12);
}

/* Control: an encoder that has a CPU path keeps working on raw frames. */
static void hybrid_i444_starts(void **state)
{
	(void)state;
#ifdef SCENARIO_UNSUPPORTED
	skip();
#else
	if (!obs_up)
		skip();
	int r = run_in_child(HYBRID_ID, VIDEO_FORMAT_I444);
	if (r != RUN_STARTED_AND_ENCODED) {
		fprintf(stderr, "encoder with a CPU encode path on I444: expected it to start, got %d\n", r);
		fail();
	}
#endif
}

static int group_setup(void **state)
{
	(void)state;
	obs_up = obs_startup("en-US", NULL, NULL);
	if (obs_up) {
		register_encoders();
		register_output();
	}
	return 0;
}

static int group_teardown(void **state)
{
	(void)state;
	if (obs_up)
		obs_shutdown();
	return 0;
}

int main(void)
{
	const struct CMUnitTest tests[] = {
		cmocka_unit_test(hybrid_i444_starts),
		cmocka_unit_test(tex_only_i444_refused),
		cmocka_unit_test(tex_only_i420_refused),
		cmocka_unit_test(tex_only_bgra_refused),
		cmocka_unit_test(tex_only_nv12_without_textures_refused),
	};
	return cmocka_run_group_tests(tests, group_setup, group_teardown);
}
