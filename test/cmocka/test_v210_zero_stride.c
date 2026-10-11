/*
 * V210 zero-stride boundary regression, preserved from fork PR #14
 * (branch port/video-frame, superseded by upstream obs-rust/obs-studio#182).
 *
 * video_frame_get_linesizes computes V210's single-plane stride as
 *   align_uint32(&width, 48);  linesize[0] = ((width + 5) / 6) * 16;
 * entirely in u32. At width 1610612689 (0x5FFFFFD1) the align step yields
 * 1610612736 (0x60000000) and the multiply wraps u32 to exactly 0. Every
 * plane then reports linesize 0, so video_frame_init computes a total size
 * of 0 and calls bmalloc(0), which hits os_breakpoint()/bcrash() in
 * libobs/util/bmem.c.
 *
 * Characterized, not endorsed: the crash is the current C behavior and the
 * pins below must keep matching whatever implementation replaces it.
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

/* The proptest-found boundary input from the fork port's parity run. */
#define V210_ZERO_STRIDE_WIDTH 1610612689u /* 0x5FFFFFD1 */
/* Second input reaching the same wrapped-to-zero stride. */
#define V210_ZERO_STRIDE_WIDTH2 1610612736u /* 0x60000000 */

/* Sanity pin: an ordinary width still produces the quirky (non-pow2 mask)
 * aligned stride — width 48 aligns to 80, giving ((80+5)/6)*16 = 224. */
static void v210_sane_stride(void **state)
{
	UNUSED_PARAMETER(state);

	struct video_frame frame;
	memset(&frame, 0xAB, sizeof(frame));

	video_frame_init(&frame, VIDEO_FORMAT_V210, 48, 2);
	assert_int_equal(frame.linesize[0], 224);
	assert_non_null(frame.data[0]);
	for (int p = 1; p < MAX_AV_PLANES; p++) {
		assert_int_equal(frame.linesize[p], 0);
		assert_null(frame.data[p]);
	}
	video_frame_free(&frame);
}

/* At the wrap boundary the stride is 0, every plane is empty, and
 * video_frame_init calls bmalloc(0) -> os_breakpoint()/bcrash(). Checked
 * in a child process, same pattern as the VIDEO_FORMAT_NONE crash pin.
 * POSIX only; Windows aborts inside the test's own process. */
static void v210_zero_stride_death(uint32_t width)
{
#ifdef _WIN32
	UNUSED_PARAMETER(width);
	skip();
#else
	pid_t pid = fork();
	assert_true(pid >= 0);
	if (pid == 0) {
		struct video_frame frame;
		memset(&frame, 0xAB, sizeof(frame));
		video_frame_init(&frame, VIDEO_FORMAT_V210, width, 480);
		/* if this line runs the crash did not happen */
		_exit(0);
	}
	int status = 0;
	assert_int_equal(waitpid(pid, &status, 0), pid);
	assert_false(WIFEXITED(status) && WEXITSTATUS(status) == 0);
	assert_true(WIFSIGNALED(status));
#endif
}

static void v210_zero_stride_death_boundary(void **state)
{
	UNUSED_PARAMETER(state);
	v210_zero_stride_death(V210_ZERO_STRIDE_WIDTH);
}

static void v210_zero_stride_death_aligned(void **state)
{
	UNUSED_PARAMETER(state);
	v210_zero_stride_death(V210_ZERO_STRIDE_WIDTH2);
}

int main(void)
{
	const struct CMUnitTest tests[] = {
		cmocka_unit_test(v210_sane_stride),
		cmocka_unit_test(v210_zero_stride_death_boundary),
		cmocka_unit_test(v210_zero_stride_death_aligned),
	};
	return cmocka_run_group_tests(tests, NULL, NULL);
}
