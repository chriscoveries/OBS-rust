/* Characterization test for libobs/media-io/video-io.c.
 *
 * Pins the CURRENT C behavior of the video_output API ahead of the
 * obs-media-io port. Cases that pin surprising behavior are marked
 * "characterized, not endorsed".
 *
 * video_output_open starts a thread that calls profile_store_name on
 * obs->name_store before it waits, so the behavioral pins need obs_startup
 * (a display on Linux; the cmake fragment runs them under xvfb). The
 * parameter and null-handle pins do not.
 *
 * video_output_get_skipped_frames(NULL) and video_output_get_total_frames(NULL)
 * dereference the handle. They are not called.
 *
 * Six functions in video-io.h are not EXPORT, and libobs is built with
 * hidden visibility, so this binary cannot link them. They are pinned in
 * test_video_io_hidden.c, which compiles video-io.c into the test.
 */
#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <cmocka.h>

#include <string.h>

#ifndef _WIN32
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

#include <obs.h>
#include <media-io/video-frame.h>
#include <media-io/video-io.h>
#include <util/threading.h>
#include <util/util_uint64.h>

#define GATE_MAX 8

static int obs_up;

struct gate {
	os_sem_t *hits_sem;
	os_event_t *release;
	int hits;
	uint64_t ts[GATE_MAX];
	uint32_t linesize0;
	uint8_t byte0[GATE_MAX];
};

static void on_frame(void *param, struct video_data *frame)
{
	struct gate *g = param;
	int i = g->hits++;

	if (i < GATE_MAX) {
		g->ts[i] = frame->timestamp;
		g->byte0[i] = frame->data[0] ? frame->data[0][0] : 0;
	}
	g->linesize0 = frame->linesize[0];
	os_sem_post(g->hits_sem);
	os_event_wait(g->release);
}

static struct video_output_info rgba_info(const char *name, uint32_t width, uint32_t height, size_t cache)
{
	struct video_output_info info;

	memset(&info, 0, sizeof(info));
	info.name = name;
	info.format = VIDEO_FORMAT_RGBA;
	info.fps_num = 30;
	info.fps_den = 1;
	info.width = width;
	info.height = height;
	info.cache_size = cache;
	info.colorspace = VIDEO_CS_709;
	info.range = VIDEO_RANGE_FULL;
	return info;
}

static void gate_init(struct gate *g, bool block)
{
	memset(g, 0, sizeof(*g));
	assert_int_equal(os_sem_init(&g->hits_sem, 0), 0);
	assert_int_equal(os_event_init(&g->release, OS_EVENT_TYPE_MANUAL), 0);
	if (!block)
		os_event_signal(g->release);
}

/* Unblock a callback still inside on_frame, then join the video thread. */
static void finish(video_t *video, struct gate *g)
{
	os_event_signal(g->release);
	video_output_close(video);
	os_sem_destroy(g->hits_sem);
	os_event_destroy(g->release);
}

static void open_invalid_params(void **state)
{
	struct video_output_info base = rgba_info("bad", 4, 4, 2);
	struct video_output_info bads[4];

	UNUSED_PARAMETER(state);
	bads[0] = base;
	bads[0].width = 0;
	bads[1] = base;
	bads[1].height = 0;
	bads[2] = base;
	bads[2].fps_num = 0;
	bads[3] = base;
	bads[3].fps_den = 0;

	for (size_t i = 0; i < 4; i++) {
		video_t *video = (video_t *)(size_t)0xdeadbeef;

		assert_int_equal(video_output_open(&video, &bads[i]), VIDEO_OUTPUT_INVALIDPARAM);
		assert_ptr_equal(video, (video_t *)(size_t)0xdeadbeef);
	}
}

static void null_handles(void **state)
{
	struct video_frame frame;

	UNUSED_PARAMETER(state);
	memset(&frame, 0, sizeof(frame));

	/* connect and connect2 call get_root before the NULL check, so a NULL
	 * video segfaults. Characterized, not endorsed. Not called here. */
	video_output_disconnect(NULL, on_frame, NULL);
	assert_false(video_output_disconnect2(NULL, on_frame, NULL));
	assert_false(video_output_active(NULL));
	assert_null(video_output_get_info(NULL));
	assert_false(video_output_lock_frame(NULL, &frame, 1, 0));
	video_output_unlock_frame(NULL);
	assert_int_equal(video_output_get_frame_time(NULL), 0);
	video_output_stop(NULL);
	assert_true(video_output_stopped(NULL));
	assert_int_equal(video_output_get_format(NULL), VIDEO_FORMAT_NONE);
	assert_int_equal(video_output_get_width(NULL), 0);
	assert_int_equal(video_output_get_height(NULL), 0);
	assert_true(video_output_get_frame_rate(NULL) == 0.0);
	video_output_close(NULL);
}

/* Without a running obs core the video thread dereferences obs->name_store
 * on entry. Characterized, not endorsed. POSIX only. */
static void open_crashes_without_obs_core(void **state)
{
	UNUSED_PARAMETER(state);

#ifdef _WIN32
	skip();
#else
	pid_t pid;
	int status = 0;

	if (obs_up)
		skip();

	pid = fork();
	assert_true(pid >= 0);
	if (pid == 0) {
		video_t *video = NULL;
		struct video_output_info info = rgba_info("crash", 4, 4, 1);
		os_event_t *never = NULL;

		signal(SIGSEGV, SIG_DFL);
		signal(SIGBUS, SIG_DFL);
		signal(SIGALRM, SIG_DFL);
		alarm(2);
		assert_int_equal(video_output_open(&video, &info), VIDEO_OUTPUT_SUCCESS);
		os_event_init(&never, OS_EVENT_TYPE_MANUAL);
		os_event_wait(never);
		_exit(0);
	}
	assert_int_equal(waitpid(pid, &status, 0), pid);
	assert_true(WIFSIGNALED(status));
	assert_true(WTERMSIG(status) == SIGSEGV || WTERMSIG(status) == SIGBUS);
#endif
}

static void open_getters(void **state)
{
	video_t *video = NULL;
	struct video_output_info info = rgba_info("getters", 8, 4, 100);
	const struct video_output_info *got;
	uint64_t frame_time = util_mul_div64(1000000000ULL, info.fps_den, info.fps_num);

	UNUSED_PARAMETER(state);
	assert_true(obs_up);
	assert_int_equal(video_output_open(&video, &info), VIDEO_OUTPUT_SUCCESS);
	assert_non_null(video);

	got = video_output_get_info(video);
	assert_non_null(got);
	/* The name pointer is stored, not copied. */
	assert_ptr_equal(got->name, info.name);
	assert_int_equal(got->format, VIDEO_FORMAT_RGBA);
	assert_int_equal(got->fps_num, 30);
	assert_int_equal(got->fps_den, 1);
	assert_int_equal(got->width, 8);
	assert_int_equal(got->height, 4);
	/* cache_size above MAX_CACHE_SIZE (16) is clamped. */
	assert_int_equal(got->cache_size, 16);
	assert_int_equal(got->colorspace, VIDEO_CS_709);
	assert_int_equal(got->range, VIDEO_RANGE_FULL);

	assert_int_equal(video_output_get_format(video), VIDEO_FORMAT_RGBA);
	assert_int_equal(video_output_get_width(video), 8);
	assert_int_equal(video_output_get_height(video), 4);
	assert_true(video_output_get_frame_rate(video) == 30.0);
	assert_true(video_output_get_frame_time(video) == frame_time);
	assert_false(video_output_stopped(video));
	assert_false(video_output_active(video));
	assert_int_equal(video_output_get_skipped_frames(video), 0);
	assert_int_equal(video_output_get_total_frames(video), 0);

	video_output_stop(video);
	assert_true(video_output_stopped(video));
	video_output_stop(video);
	assert_true(video_output_stopped(video));
	video_output_close(video);
}

static void cache_size_zero_lock_fails(void **state)
{
	video_t *video = NULL;
	struct video_output_info info = rgba_info("empty-cache", 4, 4, 0);
	struct video_frame frame;

	UNUSED_PARAMETER(state);
	assert_true(obs_up);
	assert_int_equal(video_output_open(&video, &info), VIDEO_OUTPUT_SUCCESS);
	assert_int_equal(video_output_get_info(video)->cache_size, 0);
	/* available_frames starts at 0, so the first lock fails. Unlocking
	 * would wrap available_frames; this test does not unlock. */
	assert_false(video_output_lock_frame(video, &frame, 1, 1));
	video_output_close(video);
}

static void connect_delivers_one_frame(void **state)
{
	video_t *video = NULL;
	struct video_output_info info = rgba_info("one", 4, 4, 2);
	struct gate g;
	struct video_frame frame;

	UNUSED_PARAMETER(state);
	assert_true(obs_up);
	gate_init(&g, false);
	assert_int_equal(video_output_open(&video, &info), VIDEO_OUTPUT_SUCCESS);
	assert_true(video_output_connect(video, NULL, on_frame, &g));
	assert_false(video_output_connect(video, NULL, on_frame, &g));
	assert_true(video_output_active(video));

	assert_true(video_output_lock_frame(video, &frame, 1, 1000));
	assert_non_null(frame.data[0]);
	assert_int_equal(frame.linesize[0], 16);
	frame.data[0][0] = 0xAB;
	video_output_unlock_frame(video);

	assert_int_equal(os_sem_wait(g.hits_sem), 0);
	video_output_stop(video);
	assert_int_equal(g.hits, 1);
	assert_true(g.ts[0] == 1000);
	assert_int_equal(g.linesize0, 16);
	assert_int_equal(g.byte0[0], 0xAB);
	assert_int_equal(video_output_get_total_frames(video), 1);
	assert_int_equal(video_output_get_skipped_frames(video), 0);

	assert_true(video_output_disconnect2(video, on_frame, &g));
	assert_false(video_output_disconnect2(video, on_frame, &g));
	assert_false(video_output_active(video));
	finish(video, &g);
}

static void connect_rejects(void **state)
{
	video_t *video = NULL;
	struct video_output_info info = rgba_info("reject", 4, 4, 2);
	struct gate g;
	struct video_scale_info ayuv;

	UNUSED_PARAMETER(state);
	assert_true(obs_up);
	gate_init(&g, false);
	assert_int_equal(video_output_open(&video, &info), VIDEO_OUTPUT_SUCCESS);

	assert_false(video_output_connect(video, NULL, NULL, &g));
	assert_false(video_output_connect2(video, NULL, 0, on_frame, &g));
	assert_false(video_output_disconnect2(video, NULL, &g));

	memset(&ayuv, 0, sizeof(ayuv));
	ayuv.format = VIDEO_FORMAT_AYUV;
	ayuv.width = 4;
	ayuv.height = 4;
	ayuv.range = VIDEO_RANGE_FULL;
	ayuv.colorspace = VIDEO_CS_709;
	/* AYUV has no FFmpeg pixel format. Characterized, not endorsed:
	 * connect fails rather than delivering the source frame. */
	assert_false(video_output_connect(video, &ayuv, on_frame, &g));
	assert_false(video_output_active(video));

	finish(video, &g);
}

static void zero_conversion_size_uses_output(void **state)
{
	video_t *video = NULL;
	struct video_output_info info = rgba_info("zero-conv", 4, 4, 2);
	struct video_scale_info conv;
	struct gate g;
	struct video_frame frame;

	UNUSED_PARAMETER(state);
	assert_true(obs_up);
	gate_init(&g, false);
	assert_int_equal(video_output_open(&video, &info), VIDEO_OUTPUT_SUCCESS);

	memset(&conv, 0, sizeof(conv));
	conv.format = VIDEO_FORMAT_RGBA;
	conv.range = VIDEO_RANGE_FULL;
	conv.colorspace = VIDEO_CS_709;
	assert_true(video_output_connect(video, &conv, on_frame, &g));

	assert_true(video_output_lock_frame(video, &frame, 1, 7));
	frame.data[0][0] = 0x11;
	video_output_unlock_frame(video);
	assert_int_equal(os_sem_wait(g.hits_sem), 0);
	video_output_stop(video);
	assert_int_equal(g.linesize0, 16);
	assert_int_equal(g.byte0[0], 0x11);
	finish(video, &g);
}

static void srgb_matches_709_without_scaler(void **state)
{
	video_t *video = NULL;
	struct video_output_info info = rgba_info("srgb", 4, 4, 2);
	struct video_scale_info conv;
	struct gate g;
	struct video_frame frame;

	UNUSED_PARAMETER(state);
	assert_true(obs_up);
	gate_init(&g, false);
	assert_int_equal(video_output_open(&video, &info), VIDEO_OUTPUT_SUCCESS);

	memset(&conv, 0, sizeof(conv));
	conv.format = VIDEO_FORMAT_RGBA;
	conv.width = 4;
	conv.height = 4;
	conv.range = VIDEO_RANGE_FULL;
	/* SRGB collapses to 709, so this does not build a scaler and the
	 * source byte is delivered unchanged. */
	conv.colorspace = VIDEO_CS_SRGB;
	assert_true(video_output_connect(video, &conv, on_frame, &g));

	assert_true(video_output_lock_frame(video, &frame, 1, 9));
	frame.data[0][0] = 0x22;
	video_output_unlock_frame(video);
	assert_int_equal(os_sem_wait(g.hits_sem), 0);
	video_output_stop(video);
	assert_int_equal(g.linesize0, 16);
	assert_int_equal(g.byte0[0], 0x22);
	finish(video, &g);
}

static void scale_changes_linesize(void **state)
{
	video_t *video = NULL;
	struct video_output_info info = rgba_info("scale", 4, 4, 2);
	struct video_scale_info conv;
	struct gate g;
	struct video_frame frame;

	UNUSED_PARAMETER(state);
	assert_true(obs_up);
	gate_init(&g, false);
	assert_int_equal(video_output_open(&video, &info), VIDEO_OUTPUT_SUCCESS);

	memset(&conv, 0, sizeof(conv));
	conv.format = VIDEO_FORMAT_RGBA;
	conv.width = 2;
	conv.height = 2;
	conv.range = VIDEO_RANGE_FULL;
	conv.colorspace = VIDEO_CS_709;
	assert_true(video_output_connect(video, &conv, on_frame, &g));

	assert_true(video_output_lock_frame(video, &frame, 1, 11));
	frame.data[0][0] = 0x33;
	video_output_unlock_frame(video);
	assert_int_equal(os_sem_wait(g.hits_sem), 0);
	video_output_stop(video);
	assert_int_equal(g.linesize0, 8);
	finish(video, &g);
}

static void count_repeats_frame(void **state)
{
	video_t *video = NULL;
	struct video_output_info info = rgba_info("repeat", 4, 4, 2);
	struct gate g;
	struct video_frame frame;
	uint64_t frame_time;

	UNUSED_PARAMETER(state);
	assert_true(obs_up);
	gate_init(&g, false);
	assert_int_equal(video_output_open(&video, &info), VIDEO_OUTPUT_SUCCESS);
	frame_time = video_output_get_frame_time(video);
	assert_true(video_output_connect(video, NULL, on_frame, &g));

	assert_true(video_output_lock_frame(video, &frame, 2, 5000));
	frame.data[0][0] = 0x44;
	video_output_unlock_frame(video);
	assert_int_equal(os_sem_wait(g.hits_sem), 0);
	assert_int_equal(os_sem_wait(g.hits_sem), 0);
	video_output_stop(video);

	assert_int_equal(g.hits, 2);
	assert_true(g.ts[0] == 5000);
	assert_true(g.ts[1] == 5000 + frame_time);
	assert_int_equal(g.byte0[0], 0x44);
	assert_int_equal(g.byte0[1], 0x44);
	assert_int_equal(video_output_get_total_frames(video), 2);
	assert_int_equal(video_output_get_skipped_frames(video), 0);
	finish(video, &g);
}

static void frame_rate_divisor_skips_every_other(void **state)
{
	video_t *video = NULL;
	struct video_output_info info = rgba_info("div2", 4, 4, 4);
	struct gate g;
	struct video_frame frame;
	uint64_t stamps[3] = {1000, 2000, 3000};
	uint8_t bytes[3] = {1, 2, 3};

	UNUSED_PARAMETER(state);
	assert_true(obs_up);
	gate_init(&g, false);
	assert_int_equal(video_output_open(&video, &info), VIDEO_OUTPUT_SUCCESS);
	assert_true(video_output_connect2(video, NULL, 2, on_frame, &g));

	for (int i = 0; i < 3; i++) {
		assert_true(video_output_lock_frame(video, &frame, 1, stamps[i]));
		frame.data[0][0] = bytes[i];
		video_output_unlock_frame(video);
	}
	assert_int_equal(os_sem_wait(g.hits_sem), 0);
	assert_int_equal(os_sem_wait(g.hits_sem), 0);
	video_output_stop(video);

	assert_int_equal(g.hits, 2);
	assert_true(g.ts[0] == 1000);
	assert_true(g.ts[1] == 3000);
	assert_int_equal(g.byte0[0], 1);
	assert_int_equal(g.byte0[1], 3);
	assert_int_equal(video_output_get_total_frames(video), 3);
	finish(video, &g);
}

static void full_cache_counts_a_skip(void **state)
{
	video_t *video = NULL;
	struct video_output_info info = rgba_info("full", 4, 4, 1);
	struct gate g;
	struct video_frame frame;
	uint64_t frame_time;

	UNUSED_PARAMETER(state);
	assert_true(obs_up);
	gate_init(&g, true);
	assert_int_equal(video_output_open(&video, &info), VIDEO_OUTPUT_SUCCESS);
	frame_time = video_output_get_frame_time(video);
	assert_true(video_output_connect(video, NULL, on_frame, &g));

	assert_true(video_output_lock_frame(video, &frame, 1, 1000));
	frame.data[0][0] = 0xAB;
	video_output_unlock_frame(video);
	assert_int_equal(os_sem_wait(g.hits_sem), 0);

	/* The callback is still inside on_frame, so the cache is full. */
	assert_false(video_output_lock_frame(video, &frame, 1, 999));
	os_event_signal(g.release);
	assert_int_equal(os_sem_wait(g.hits_sem), 0);
	video_output_stop(video);

	assert_int_equal(g.hits, 2);
	assert_true(g.ts[0] == 1000);
	assert_true(g.ts[1] == 1000 + frame_time);
	assert_int_equal(g.byte0[0], 0xAB);
	assert_int_equal(video_output_get_skipped_frames(video), 1);
	assert_int_equal(video_output_get_total_frames(video), 2);
	finish(video, &g);
}

static void connect_resets_frame_counters(void **state)
{
	video_t *video = NULL;
	struct video_output_info info = rgba_info("reset", 4, 4, 2);
	struct gate g;
	struct video_frame frame;

	UNUSED_PARAMETER(state);
	assert_true(obs_up);
	gate_init(&g, false);
	assert_int_equal(video_output_open(&video, &info), VIDEO_OUTPUT_SUCCESS);
	assert_true(video_output_connect(video, NULL, on_frame, &g));

	assert_true(video_output_lock_frame(video, &frame, 1, 1000));
	frame.data[0][0] = 1;
	video_output_unlock_frame(video);
	assert_int_equal(os_sem_wait(g.hits_sem), 0);
	video_output_stop(video);
	assert_int_equal(video_output_get_total_frames(video), 1);

	assert_true(video_output_disconnect2(video, on_frame, &g));
	assert_false(video_output_active(video));
	/* Disconnect leaves the counters. The next first raw input clears
	 * them. gpu_refs stays 0 on this path. */
	assert_int_equal(video_output_get_total_frames(video), 1);
	assert_true(video_output_connect(video, NULL, on_frame, &g));
	assert_int_equal(video_output_get_total_frames(video), 0);
	assert_true(video_output_active(video));
	finish(video, &g);
}

static int group_setup(void **state)
{
	UNUSED_PARAMETER(state);
	obs_up = obs_startup("en-US", NULL, NULL);
	return 0;
}

static int group_teardown(void **state)
{
	UNUSED_PARAMETER(state);
	if (obs_up)
		obs_shutdown();
	return 0;
}

int main(void)
{
	const struct CMUnitTest tests[] = {
		cmocka_unit_test(open_invalid_params),
		cmocka_unit_test(null_handles),
		cmocka_unit_test(open_crashes_without_obs_core),
		cmocka_unit_test(open_getters),
		cmocka_unit_test(cache_size_zero_lock_fails),
		cmocka_unit_test(connect_delivers_one_frame),
		cmocka_unit_test(connect_rejects),
		cmocka_unit_test(zero_conversion_size_uses_output),
		cmocka_unit_test(srgb_matches_709_without_scaler),
		cmocka_unit_test(scale_changes_linesize),
		cmocka_unit_test(count_repeats_frame),
		cmocka_unit_test(frame_rate_divisor_skips_every_other),
		cmocka_unit_test(full_cache_counts_a_skip),
		cmocka_unit_test(connect_resets_frame_counters),
	};

	return cmocka_run_group_tests(tests, group_setup, group_teardown);
}
