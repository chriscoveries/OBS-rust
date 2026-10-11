#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <setjmp.h>
#include <cmocka.h>

#include <linux/videodev2.h>

#include "v4l2-input-caps.h"

/* Selecting a video format in the V4L2 source's properties opens the device
 * and asks for the capabilities of the selected input. Devices that have no
 * such input, like the Raspberry Pi's ISP and codec nodes, fail the query,
 * and the device was left open each time. libv4l2 tracks at most 16 open
 * devices per process, so after a few refreshes every open fails with
 * "error attempting to open more then 16 video devices" and no camera works
 * until OBS is restarted (obsproject/obs-studio#5422, reported by
 * jclab-joseph, gruzilla, ethagnawl, vicary and mh5). */

/* libv4l2's V4L2_MAX_DEVICES */
#define MAX_DEVICES 16
#define FIRST_FD 100

static bool open_fds[MAX_DEVICES];
static int opens;
static int closes;
static int queries;
static int query_result;
static uint32_t query_caps;

static int fake_open(const char *device)
{
	(void)device;
	for (int i = 0; i < MAX_DEVICES; i++) {
		if (!open_fds[i]) {
			open_fds[i] = true;
			opens++;
			return FIRST_FD + i;
		}
	}
	/* libv4l2: "error attempting to open more then 16 video devices" */
	return -1;
}

static int fake_close(int dev)
{
	int i = dev - FIRST_FD;
	if (i < 0 || i >= MAX_DEVICES || !open_fds[i]) {
		fprintf(stderr, "closed fd %d, which is not open\n", dev);
		fail();
	}
	open_fds[i] = false;
	closes++;
	return 0;
}

static int fake_failing_open(const char *device)
{
	(void)device;
	opens++;
	return -1;
}

static int fake_get_input_caps(int dev, int input, uint32_t *caps)
{
	(void)input;
	int i = dev - FIRST_FD;
	if (i < 0 || i >= MAX_DEVICES || !open_fds[i]) {
		fprintf(stderr, "queried fd %d, which is not open\n", dev);
		fail();
	}
	queries++;
	if (query_result < 0)
		return query_result;
	*caps = query_caps;
	return 0;
}

static const struct v4l2_input_caps_ops ops = {
	.open = fake_open,
	.close = fake_close,
	.get_input_caps = fake_get_input_caps,
};

static int open_count(void)
{
	int n = 0;
	for (int i = 0; i < MAX_DEVICES; i++)
		n += open_fds[i];
	return n;
}

static int setup(void **state)
{
	(void)state;
	for (int i = 0; i < MAX_DEVICES; i++)
		open_fds[i] = false;
	opens = closes = queries = 0;
	query_result = 0;
	query_caps = 0;
	return 0;
}

static void caps_query_failure_closes_device(void **state)
{
	(void)state;
	query_result = -1;

	uint32_t caps = 0;
	int dev = v4l2_open_input_caps(&ops, "/dev/video0", 1, &caps);

	assert_int_equal(dev, -1);
	assert_int_equal(opens, 1);
	assert_int_equal(queries, 1);
	if (open_count() != 0) {
		fprintf(stderr, "failed caps query left %d device(s) open (%d opens, %d closes)\n", open_count(), opens,
			closes);
		fail();
	}
}

static void repeated_caps_query_failures_keep_opening(void **state)
{
	(void)state;
	query_result = -1;

	/* the properties dialog is refreshed, or the format changed, many
	 * times; every refresh must still be able to open the device */
	for (int i = 0; i < 4 * MAX_DEVICES; i++) {
		uint32_t caps = 0;
		v4l2_open_input_caps(&ops, "/dev/video0", 1, &caps);
		if (queries != i + 1) {
			fprintf(stderr, "refresh %d could not open the device: %d devices still open\n", i + 1,
				open_count());
			fail();
		}
	}
	assert_int_equal(open_count(), 0);
}

static void caps_query_success_returns_open_device(void **state)
{
	(void)state;
	query_caps = V4L2_IN_CAP_STD | V4L2_IN_CAP_NATIVE_SIZE;

	uint32_t caps = 0;
	int dev = v4l2_open_input_caps(&ops, "/dev/video0", 0, &caps);

	assert_true(dev >= FIRST_FD);
	assert_int_equal(caps, V4L2_IN_CAP_STD);
	assert_int_equal(open_count(), 1);
	assert_int_equal(closes, 0);

	fake_close(dev);
	assert_int_equal(open_count(), 0);
}

static void open_failure_skips_query(void **state)
{
	(void)state;
	const struct v4l2_input_caps_ops failing = {
		.open = fake_failing_open,
		.close = fake_close,
		.get_input_caps = fake_get_input_caps,
	};

	uint32_t caps = 0;
	int dev = v4l2_open_input_caps(&failing, "/dev/video0", 0, &caps);

	assert_int_equal(dev, -1);
	assert_int_equal(queries, 0);
	assert_int_equal(closes, 0);
}

int main(void)
{
	const struct CMUnitTest tests[] = {
		cmocka_unit_test_setup(caps_query_failure_closes_device, setup),
		cmocka_unit_test_setup(repeated_caps_query_failures_keep_opening, setup),
		cmocka_unit_test_setup(caps_query_success_returns_open_device, setup),
		cmocka_unit_test_setup(open_failure_skips_query, setup),
	};

	return cmocka_run_group_tests(tests, NULL, NULL);
}
