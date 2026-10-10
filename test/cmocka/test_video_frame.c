/*
 * Characterization test for libobs/media-io/video-frame.c (and the static
 * inline helpers in video-frame.h). It pins the CURRENT C behavior,
 * including odd cases, ahead of the Rust port. Cases that pin surprising
 * behavior are marked "characterized, not endorsed".
 *
 * video_frame_get_linesizes and video_frame_get_plane_heights are not
 * exported, so they are pinned through the observable state video_frame_init
 * leaves behind (per-plane linesize and data pointer offsets).
 */
#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <cmocka.h>

#ifndef _WIN32
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

#include <media-io/video-frame.h>
#include <util/bmem.h>

#define FORMAT_COUNT (VIDEO_FORMAT_R10L + 1)

/* Expected linesize table, derived from the C implementation. Each entry is
 * the per-plane multiplier applied to the "effective" width unless noted. */
static void expected_linesizes(uint32_t out[MAX_AV_PLANES], enum video_format format, uint32_t width)
{
	memset(out, 0, sizeof(uint32_t) * MAX_AV_PLANES);

	uint32_t half = (width + 1) / 2;
	switch (format) {
	default:
		break;
	case VIDEO_FORMAT_BGR3:
		out[0] = width * 3;
		break;
	case VIDEO_FORMAT_RGBA:
	case VIDEO_FORMAT_BGRA:
	case VIDEO_FORMAT_BGRX:
	case VIDEO_FORMAT_AYUV:
	case VIDEO_FORMAT_R10L:
		out[0] = width * 4;
		break;
	case VIDEO_FORMAT_P416:
		out[0] = width * 2;
		out[1] = width * 4;
		break;
	case VIDEO_FORMAT_I420:
	case VIDEO_FORMAT_I422:
		out[0] = width;
		out[1] = half;
		out[2] = half;
		break;
	case VIDEO_FORMAT_I210:
	case VIDEO_FORMAT_I010:
		out[0] = width * 2;
		out[1] = width;
		out[2] = width;
		break;
	case VIDEO_FORMAT_I40A:
	case VIDEO_FORMAT_I42A:
		out[0] = width;
		out[1] = half;
		out[2] = half;
		out[3] = width;
		break;
	case VIDEO_FORMAT_YVYU:
	case VIDEO_FORMAT_YUY2:
	case VIDEO_FORMAT_UYVY:
		out[0] = width * 2;
		break;
	case VIDEO_FORMAT_P010:
	case VIDEO_FORMAT_P216:
		out[0] = width * 2;
		out[1] = width * 2;
		break;
	case VIDEO_FORMAT_I412:
		out[0] = width * 2;
		out[1] = width * 2;
		out[2] = width * 2;
		break;
	case VIDEO_FORMAT_YA2L:
		out[0] = width * 2;
		out[1] = width * 2;
		out[2] = width * 2;
		out[3] = width * 2;
		break;
	case VIDEO_FORMAT_Y800:
		out[0] = width;
		break;
	case VIDEO_FORMAT_NV12:
		out[0] = width;
		out[1] = width;
		break;
	case VIDEO_FORMAT_I444:
		out[0] = width;
		out[1] = width;
		out[2] = width;
		break;
	case VIDEO_FORMAT_YUVA:
		out[0] = width;
		out[1] = width;
		out[2] = width;
		out[3] = width;
		break;
	case VIDEO_FORMAT_V210: {
		uint32_t w = (width + 47) & ~47u; /* aligned up to 48 */
		out[0] = ((w + 5) / 6) * 16;
		break;
	}
	}
}

static void expected_heights(uint32_t out[MAX_AV_PLANES], enum video_format format, uint32_t height)
{
	memset(out, 0, sizeof(uint32_t) * MAX_AV_PLANES);

	uint32_t half = (height + 1) / 2;
	switch (format) {
	default:
		break;
	case VIDEO_FORMAT_I420:
	case VIDEO_FORMAT_I010:
		out[0] = height;
		out[1] = half;
		out[2] = half;
		break;
	case VIDEO_FORMAT_NV12:
	case VIDEO_FORMAT_P010:
		out[0] = height;
		out[1] = half;
		break;
	case VIDEO_FORMAT_Y800:
	case VIDEO_FORMAT_YVYU:
	case VIDEO_FORMAT_YUY2:
	case VIDEO_FORMAT_UYVY:
	case VIDEO_FORMAT_RGBA:
	case VIDEO_FORMAT_BGRA:
	case VIDEO_FORMAT_BGRX:
	case VIDEO_FORMAT_BGR3:
	case VIDEO_FORMAT_AYUV:
	case VIDEO_FORMAT_V210:
	case VIDEO_FORMAT_R10L:
		out[0] = height;
		break;
	case VIDEO_FORMAT_I444:
	case VIDEO_FORMAT_I422:
	case VIDEO_FORMAT_I210:
	case VIDEO_FORMAT_I412:
		out[0] = height;
		out[1] = height;
		out[2] = height;
		break;
	case VIDEO_FORMAT_I40A:
		out[0] = height;
		out[1] = half;
		out[2] = half;
		out[3] = height;
		break;
	case VIDEO_FORMAT_I42A:
	case VIDEO_FORMAT_YUVA:
	case VIDEO_FORMAT_YA2L:
		out[0] = height;
		out[1] = height;
		out[2] = height;
		out[3] = height;
		break;
	case VIDEO_FORMAT_P216:
	case VIDEO_FORMAT_P416:
		out[0] = height;
		out[1] = height;
		break;
	}
}

static size_t align_up(size_t v)
{
	int alignment = base_get_alignment();
	return (v + alignment - 1) & ~(size_t)(alignment - 1);
}

/* Every format: init must set per-plane linesize and place each data
 * pointer at the cumulative aligned offset pinned by linesize*height. */
static void frame_init_all_formats(void **state)
{
	UNUSED_PARAMETER(state);

	const uint32_t widths[] = {1, 3, 5, 16, 47, 48, 100, 640};
	const uint32_t heights[] = {1, 3, 5, 16, 480};

	for (int fmt = VIDEO_FORMAT_NONE; fmt < FORMAT_COUNT; fmt++) {
		for (size_t w = 0; w < sizeof(widths) / sizeof(widths[0]); w++) {
				/* VIDEO_FORMAT_NONE allocs 0 bytes and crashes (below). */
			if (fmt == VIDEO_FORMAT_NONE)
				continue;

			for (size_t h = 0; h < sizeof(heights) / sizeof(heights[0]); h++) {
				struct video_frame frame;
				memset(&frame, 0xAB, sizeof(frame));

				video_frame_init(&frame, (enum video_format)fmt, widths[w], heights[h]);

				uint32_t linesizes[MAX_AV_PLANES];
				uint32_t plane_heights[MAX_AV_PLANES];
				expected_linesizes(linesizes, (enum video_format)fmt, widths[w]);
				expected_heights(plane_heights, (enum video_format)fmt, heights[h]);

				size_t offset = 0;
				for (int p = 0; p < MAX_AV_PLANES; p++) {
					assert_int_equal(frame.linesize[p], linesizes[p]);
					if (!linesizes[p] || !plane_heights[p]) {
						assert_null(frame.data[p]);
						continue;
					}
					assert_non_null(frame.data[0]);
					assert_ptr_equal(frame.data[p], frame.data[0] + offset);
					offset += align_up((size_t)linesizes[p] * plane_heights[p]);
				}
				video_frame_free(&frame);
			}
		}
	}
}

/* VIDEO_FORMAT_NONE: every linesize stays 0, so video_frame_init calls
 * bmalloc(0), which hits os_breakpoint()/bcrash() in bmem.c. Characterized,
 * not endorsed: init on NONE is a guaranteed crash, checked in a child.
 * POSIX only; Windows aborts inside the test's own process. */
static void frame_init_none(void **state)
{
	UNUSED_PARAMETER(state);

#ifdef _WIN32
	skip();
#else
	pid_t pid = fork();
	assert_true(pid >= 0);
	if (pid == 0) {
		struct video_frame frame;
		memset(&frame, 0xAB, sizeof(frame));
		video_frame_init(&frame, VIDEO_FORMAT_NONE, 8, 8);
		/* if this line runs the crash did not happen */
		_exit(0);
	}
	int status = 0;
	assert_int_equal(waitpid(pid, &status, 0), pid);
	assert_false(WIFEXITED(status) && WEXITSTATUS(status) == 0);
	assert_true(WIFSIGNALED(status));
#endif
}

/* NULL frame is a no-op. */
static void frame_init_null(void **state)
{
	UNUSED_PARAMETER(state);
	video_frame_init(NULL, VIDEO_FORMAT_I420, 8, 8);
}

/* Equal linesizes copy plane content verbatim. */
static void frame_copy_same(void **state)
{
	UNUSED_PARAMETER(state);

	struct video_frame src, dst;
	video_frame_init(&src, VIDEO_FORMAT_I420, 4, 4);
	video_frame_init(&dst, VIDEO_FORMAT_I420, 4, 4);

	for (int p = 0; p < MAX_AV_PLANES; p++)
		if (src.data[p])
			memset(src.data[p], 0x30 + p, src.linesize[p] * (p == 0 ? 4 : 2));

	video_frame_copy(&dst, &src, VIDEO_FORMAT_I420, 4);

	for (int p = 0; p < MAX_AV_PLANES; p++) {
		if (!src.data[p])
			continue;
		uint32_t rows = (p == 0) ? 4 : 2;
		for (uint32_t y = 0; y < rows; y++)
			assert_memory_equal((char *)dst.data[p] + dst.linesize[p] * y,
					    (char *)src.data[p] + src.linesize[p] * y,
					    src.linesize[p]);
	}

	video_frame_free(&src);
	video_frame_free(&dst);
}

/* When dst linesize is larger, each row copies only src_linesize bytes;
 * the rest of dst is untouched. */
static void frame_copy_wider_dst(void **state)
{
	UNUSED_PARAMETER(state);

	struct video_frame src, dst;
	video_frame_init(&src, VIDEO_FORMAT_I420, 4, 4);
	video_frame_init(&dst, VIDEO_FORMAT_I420, 4, 4);

	/* widen dst plane 0 artificially: linesize differs but buffers match */
	uint8_t wide[16 * 4];
	memset(wide, 0xFF, sizeof(wide));
	uint8_t *real = dst.data[0];
	dst.data[0] = wide;
	dst.linesize[0] = 16;

	memset(src.data[0], 0x77, src.linesize[0] * 4);

	video_frame_copy(&dst, &src, VIDEO_FORMAT_I420, 4);

	for (uint32_t y = 0; y < 4; y++) {
		assert_memory_equal(wide + 16 * y, src.data[0], 4);
		for (int x = 4; x < 16; x++)
			assert_int_equal(wide[16 * y + x], 0xFF);
	}

	dst.data[0] = real;
	video_frame_free(&src);
	video_frame_free(&dst);
}

/* When dst linesize is smaller, each row is truncated to dst's width. */
static void frame_copy_narrower_dst(void **state)
{
	UNUSED_PARAMETER(state);

	struct video_frame src, dst;
	video_frame_init(&src, VIDEO_FORMAT_I420, 4, 4);
	video_frame_init(&dst, VIDEO_FORMAT_I420, 4, 4);

	uint8_t narrow[2 * 4];
	memset(narrow, 0xFF, sizeof(narrow));
	uint8_t *real = dst.data[0];
	dst.data[0] = narrow;
	dst.linesize[0] = 2;

	for (uint32_t y = 0; y < 4; y++)
		memset((char *)src.data[0] + src.linesize[0] * y, 0x40 + y, src.linesize[0]);

	video_frame_copy(&dst, &src, VIDEO_FORMAT_I420, 4);

	for (uint32_t y = 0; y < 4; y++)
		for (int x = 0; x < 2; x++)
			assert_int_equal(narrow[2 * y + x], 0x40 + y);

	dst.data[0] = real;
	video_frame_free(&src);
	video_frame_free(&dst);
}

/* Header-inline helpers: create/destroy roundtrip, free(NULL) tolerated. */
static void frame_inline_helpers(void **state)
{
	UNUSED_PARAMETER(state);

	struct video_frame *frame = video_frame_create(VIDEO_FORMAT_BGRA, 2, 2);
	assert_non_null(frame);
	assert_non_null(frame->data[0]);
	assert_int_equal(frame->linesize[0], 8);
	video_frame_destroy(frame);

	video_frame_free(NULL);
	video_frame_destroy(NULL);
}

int main()
{
	const struct CMUnitTest tests[] = {
		cmocka_unit_test(frame_init_all_formats),
		cmocka_unit_test(frame_init_none),
		cmocka_unit_test(frame_init_null),
		cmocka_unit_test(frame_copy_same),
		cmocka_unit_test(frame_copy_wider_dst),
		cmocka_unit_test(frame_copy_narrower_dst),
		cmocka_unit_test(frame_inline_helpers),
	};

	return cmocka_run_group_tests(tests, NULL, NULL);
}
