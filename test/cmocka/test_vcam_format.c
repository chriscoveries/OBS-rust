#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <setjmp.h>
#include <cmocka.h>

#include "vcam-format.h"

/* The virtual camera on Linux showed interleaved, distorted rows when the
 * v4l2loopback device had a fixed format of a different size, for example
 * `v4l2loopback-ctl set-caps /dev/video10 "YUYV:640x480"` with OBS set to
 * 1280x720 (upstream obsproject/obs-studio#12081, reported by
 * stephematician). VIDIOC_S_FMT answers with the format the device keeps,
 * but OBS went on writing frames of its own size, so each 1280-pixel row
 * filled two 640-pixel rows and the bottom of the picture never arrived.
 *
 * OBS must scale to the size the device accepted and write frames of that
 * size, and give up on a device that does not take YUYV. */

static struct v4l2_pix_format yuyv(uint32_t width, uint32_t height, uint32_t bytesperline)
{
	struct v4l2_pix_format pix = {0};
	pix.width = width;
	pix.height = height;
	pix.pixelformat = V4L2_PIX_FMT_YUYV;
	pix.bytesperline = bytesperline;
	pix.sizeimage = bytesperline * height;
	return pix;
}

static void expect_format(const struct vcam_format *got, uint32_t width, uint32_t height, uint32_t frame_size)
{
	if (got->width != width || got->height != height || got->frame_size != frame_size) {
		fprintf(stderr,
			"virtual camera writes %ux%u frames of %u bytes, device takes %ux%u frames of %u bytes\n",
			got->width, got->height, got->frame_size, width, height, frame_size);
		fail();
	}
}

static void device_takes_requested_size(void **state)
{
	(void)state;
	struct v4l2_pix_format pix = yuyv(1280, 720, 2560);
	struct vcam_format f;

	assert_true(vcam_format_negotiate(1280, 720, &pix, &f));
	expect_format(&f, 1280, 720, 1280 * 720 * 2);
}

static void fixed_smaller_device_gets_frames_of_its_size(void **state)
{
	(void)state;
	/* set-caps YUYV:640x480 while OBS outputs 1280x720 */
	struct v4l2_pix_format pix = yuyv(640, 480, 1280);
	struct vcam_format f;

	assert_true(vcam_format_negotiate(1280, 720, &pix, &f));
	expect_format(&f, 640, 480, 640 * 480 * 2);
}

static void fixed_larger_device_gets_frames_of_its_size(void **state)
{
	(void)state;
	struct v4l2_pix_format pix = yuyv(1920, 1080, 3840);
	struct vcam_format f;

	assert_true(vcam_format_negotiate(1280, 720, &pix, &f));
	expect_format(&f, 1920, 1080, 1920 * 1080 * 2);
}

static void device_fixed_to_other_pixel_format_is_refused(void **state)
{
	(void)state;
	struct v4l2_pix_format pix = yuyv(1280, 720, 5120);
	struct vcam_format f;

	pix.pixelformat = V4L2_PIX_FMT_BGR32;
	if (vcam_format_negotiate(1280, 720, &pix, &f)) {
		fprintf(stderr, "virtual camera would write YUYV to a device fixed to BGR4\n");
		fail();
	}
}

int main(void)
{
	const struct CMUnitTest tests[] = {
		cmocka_unit_test(device_takes_requested_size),
		cmocka_unit_test(fixed_smaller_device_gets_frames_of_its_size),
		cmocka_unit_test(fixed_larger_device_gets_frames_of_its_size),
		cmocka_unit_test(device_fixed_to_other_pixel_format_is_refused),
	};

	return cmocka_run_group_tests(tests, NULL, NULL);
}
