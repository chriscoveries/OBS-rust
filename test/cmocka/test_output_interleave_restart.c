/* The interleaver's catch-up limit belongs to one capture, not the lifetime
 * of an output. Drive registered encoders through public media APIs, repeat
 * the same backlog on one output, and count delivered packets. */
#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <cmocka.h>

#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>

#include <obs.h>
#include <media-io/video-frame.h>
#include <util/threading.h>

static struct {
	obs_output_t *output;
	obs_encoder_t *video_encoder;
	obs_encoder_t *audio_encoder;
	video_t *video;
	audio_t *audio;
	os_event_t *audio_command;
	os_event_t *audio_done;
	os_event_t *video_done;
	atomic_bool audio_enabled;
	atomic_bool shutdown;
	atomic_size_t delivered;
	uint64_t audio_timestamp;
	int64_t audio_pts;
	bool audio_packet_pending;
} harness;

static const char *test_name(void *unused)
{
	UNUSED_PARAMETER(unused);
	return "interleave restart test";
}

static void *encoder_create(obs_data_t *settings, obs_encoder_t *encoder)
{
	UNUSED_PARAMETER(settings);
	return encoder;
}

static void test_destroy(void *data)
{
	UNUSED_PARAMETER(data);
}

static size_t audio_frame_size(void *data)
{
	UNUSED_PARAMETER(data);
	return 960; /* 20 ms at 48 kHz, matching the 50 fps video. */
}

static bool encode(void *data, struct encoder_frame *frame, struct encoder_packet *packet, bool *received)
{
	static uint8_t payload;
	packet->type = obs_encoder_get_type(data);
	*received = packet->type == OBS_ENCODER_VIDEO || harness.audio_packet_pending;
	if (!*received)
		return true;

	packet->data = &payload;
	packet->size = sizeof(payload);
	packet->pts = packet->dts = packet->type == OBS_ENCODER_VIDEO ? frame->pts : harness.audio_pts;
	packet->keyframe = packet->type == OBS_ENCODER_VIDEO;
	if (packet->type == OBS_ENCODER_AUDIO)
		harness.audio_packet_pending = false;
	return true;
}

static void *output_create(obs_data_t *settings, obs_output_t *output)
{
	UNUSED_PARAMETER(settings);
	return output;
}

static bool output_start(void *data)
{
	return obs_output_can_begin_data_capture(data, 0) && obs_output_initialize_encoders(data, 0) &&
	       obs_output_begin_data_capture(data, 0);
}

static void output_stop(void *data, uint64_t timestamp)
{
	UNUSED_PARAMETER(timestamp);
	obs_output_end_data_capture(data);
}

static void output_packet(void *data, struct encoder_packet *packet)
{
	UNUSED_PARAMETER(data);
	UNUSED_PARAMETER(packet);
	atomic_fetch_add(&harness.delivered, 1);
}

static bool audio_input(void *data, uint64_t start, uint64_t end, uint64_t *timestamp, uint32_t mixers,
			struct audio_output_data *mixes)
{
	UNUSED_PARAMETER(data);
	UNUSED_PARAMETER(start);
	UNUSED_PARAMETER(end);
	UNUSED_PARAMETER(mixes); /* audio_output_open supplies zeroed silence. */
	if (!atomic_load(&harness.audio_enabled) || atomic_load(&harness.shutdown) || !(mixers & 1))
		return false;
	os_event_wait(harness.audio_command);
	if (atomic_load(&harness.shutdown))
		return false;
	*timestamp = harness.audio_timestamp;
	harness.audio_packet_pending = harness.audio_pts >= 0;
	return true;
}

/* Audio callbacks run in reverse registration order, video callbacks in
 * registration order. These observers acknowledge only after the encoder
 * and the output have finished processing the submitted media block. */
static void audio_done(void *data, size_t mix, struct audio_data *audio)
{
	UNUSED_PARAMETER(data);
	UNUSED_PARAMETER(mix);
	UNUSED_PARAMETER(audio);
	os_event_signal(harness.audio_done);
}

static void video_done(void *data, struct video_data *video)
{
	UNUSED_PARAMETER(data);
	UNUSED_PARAMETER(video);
	os_event_signal(harness.video_done);
}

static int setup(void **state)
{
	UNUSED_PARAMETER(state);
	assert_true(obs_startup("en-US", NULL, NULL));
	struct obs_video_info ovi = {
		.graphics_module = GRAPHICS_MODULE_PATH,
		.fps_num = 50,
		.fps_den = 1,
		.base_width = 64,
		.base_height = 64,
		.output_width = 64,
		.output_height = 64,
		.output_format = VIDEO_FORMAT_RGBA,
		.colorspace = VIDEO_CS_709,
		.range = VIDEO_RANGE_FULL,
		.scale_type = OBS_SCALE_BICUBIC,
	};
	assert_int_equal(obs_reset_video(&ovi), OBS_VIDEO_SUCCESS);

	struct obs_encoder_info encoder = {
		.id = "test_interleave_video",
		.type = OBS_ENCODER_VIDEO,
		.codec = "h264",
		.get_name = test_name,
		.create = encoder_create,
		.destroy = test_destroy,
		.encode = encode,
	};
	obs_register_encoder(&encoder);
	encoder.id = "test_interleave_audio";
	encoder.type = OBS_ENCODER_AUDIO;
	encoder.codec = "aac";
	encoder.get_frame_size = audio_frame_size;
	obs_register_encoder(&encoder);
	struct obs_output_info output = {
		.id = "test_interleave_output",
		.flags = OBS_OUTPUT_AV | OBS_OUTPUT_ENCODED,
		.get_name = test_name,
		.create = output_create,
		.destroy = test_destroy,
		.start = output_start,
		.stop = output_stop,
		.encoded_packet = output_packet,
	};
	obs_register_output(&output);

	assert_int_equal(os_event_init(&harness.audio_command, OS_EVENT_TYPE_AUTO), 0);
	assert_int_equal(os_event_init(&harness.audio_done, OS_EVENT_TYPE_AUTO), 0);
	assert_int_equal(os_event_init(&harness.video_done, OS_EVENT_TYPE_AUTO), 0);
	struct video_output_info vi = {
		.name = "controlled video",
		.format = VIDEO_FORMAT_RGBA,
		.fps_num = 50,
		.fps_den = 1,
		.width = 64,
		.height = 64,
		.cache_size = 2,
		.colorspace = VIDEO_CS_709,
		.range = VIDEO_RANGE_FULL,
	};
	assert_int_equal(video_output_open(&harness.video, &vi), VIDEO_OUTPUT_SUCCESS);
	struct audio_output_info ai = {
		.name = "controlled audio",
		.samples_per_sec = 48000,
		.format = AUDIO_FORMAT_FLOAT_PLANAR,
		.speakers = SPEAKERS_STEREO,
		.input_callback = audio_input,
	};
	assert_int_equal(audio_output_open(&harness.audio, &ai), AUDIO_OUTPUT_SUCCESS);
	assert_true(audio_output_connect(harness.audio, 0, NULL, audio_done, NULL));

	harness.video_encoder = obs_video_encoder_create("test_interleave_video", "video", NULL, NULL);
	harness.audio_encoder = obs_audio_encoder_create("test_interleave_audio", "audio", NULL, 0, NULL);
	harness.output = obs_output_create("test_interleave_output", "output", NULL, NULL);
	assert_non_null(harness.video_encoder);
	assert_non_null(harness.audio_encoder);
	assert_non_null(harness.output);
	obs_encoder_set_video(harness.video_encoder, harness.video);
	obs_encoder_set_audio(harness.audio_encoder, harness.audio);
	obs_output_set_video_encoder(harness.output, harness.video_encoder);
	obs_output_set_audio_encoder(harness.output, harness.audio_encoder, 0);
	return 0;
}

static int reset_output(void **state)
{
	UNUSED_PARAMETER(state);
	if (obs_output_active(harness.output))
		obs_output_force_stop(harness.output);
	video_output_disconnect(harness.video, video_done, NULL);
	obs_output_release(harness.output);
	harness.output = obs_output_create("test_interleave_output", "output", NULL, NULL);
	assert_non_null(harness.output);
	obs_output_set_video_encoder(harness.output, harness.video_encoder);
	obs_output_set_audio_encoder(harness.output, harness.audio_encoder, 0);
	atomic_store(&harness.delivered, 0);
	return 0;
}

static int teardown(void **state)
{
	UNUSED_PARAMETER(state);
	atomic_store(&harness.shutdown, true);
	if (harness.audio_command)
		os_event_signal(harness.audio_command);
	if (harness.output && obs_output_active(harness.output))
		obs_output_force_stop(harness.output);
	obs_output_release(harness.output);
	obs_encoder_release(harness.video_encoder);
	obs_encoder_release(harness.audio_encoder);
	audio_output_close(harness.audio);
	video_output_close(harness.video);
	os_event_destroy(harness.audio_command);
	os_event_destroy(harness.audio_done);
	os_event_destroy(harness.video_done);
	if (obs_initialized())
		obs_shutdown();
	return 0;
}

static void submit_audio(uint64_t timestamp, int64_t pts)
{
	harness.audio_timestamp = timestamp;
	harness.audio_pts = pts;
	os_event_signal(harness.audio_command);
	assert_int_equal(os_event_timedwait(harness.audio_done, 5000), 0);
}

static void submit_video(uint64_t timestamp)
{
	struct video_frame frame;
	assert_true(video_output_lock_frame(harness.video, &frame, 1, timestamp));
	memset(frame.data[0], 0, frame.linesize[0] * 64);
	video_output_unlock_frame(harness.video);
	assert_int_equal(os_event_timedwait(harness.video_done, 5000), 0);
}

static void check_catch_up_limit(size_t remaining, size_t expected)
{
	size_t sent[2];
	for (size_t capture = 0; capture < 2; capture++) {
		size_t before_capture = atomic_load(&harness.delivered);
		assert_true(obs_output_start(harness.output));
		assert_true(video_output_connect(harness.video, NULL, video_done, NULL));
		atomic_store(&harness.audio_enabled, true);

		const uint64_t origin = 1000000000ULL;
		/* Preroll lets the paired video encoder receive its first frame. */
		submit_audio(origin, -1);
		submit_video(origin);
		submit_audio(origin + 1024ULL * 1000000000ULL / 48000, 0);
		for (size_t frame = 1; frame <= remaining + 1; frame++)
			submit_video(origin + frame * 20000000ULL);

		/* The later audio makes all queued video packets streamable. After
		 * the ordinary send, only a backlog strictly above 4 needs catch-up. */
		size_t before = atomic_load(&harness.delivered);
		assert_int_equal(before, before_capture + 2);
		submit_audio(origin + 2048ULL * 1000000000ULL / 48000, 48000);
		sent[capture] = atomic_load(&harness.delivered) - before;
		print_message("capture %zu: %zu remaining, delivered %zu packets (expected %zu)\n", capture + 1,
			      remaining, sent[capture], expected);
		obs_output_force_stop(harness.output);
		video_output_disconnect(harness.video, video_done, NULL);
	}
	for (size_t capture = 0; capture < 2; capture++)
		assert_int_equal(sent[capture], expected);
}

static void catch_up_at_exact_limit(void **state)
{
	UNUSED_PARAMETER(state);
	check_catch_up_limit(4, 1);
}

static void catch_up_above_limit(void **state)
{
	UNUSED_PARAMETER(state);
	check_catch_up_limit(5, 2);
}

static void catch_up_limit_resets_on_restart(void **state)
{
	UNUSED_PARAMETER(state);
	/* Six remaining also stays below the stale second-capture limit 8. */
	check_catch_up_limit(6, 2);
}

int main(void)
{
	if (!getenv("DISPLAY"))
		return 77; /* Run with xvfb-run on headless Linux hosts. */
	const struct CMUnitTest tests[] = {
		cmocka_unit_test_setup_teardown(catch_up_at_exact_limit, reset_output, NULL),
		cmocka_unit_test_setup_teardown(catch_up_above_limit, reset_output, NULL),
		cmocka_unit_test_setup_teardown(catch_up_limit_resets_on_restart, reset_output, NULL),
	};
	return cmocka_run_group_tests(tests, setup, teardown);
}
