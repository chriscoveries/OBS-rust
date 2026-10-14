/* Regression test for obsproject/obs-studio#11696 (reported by sionzee): an
 * Image Slide Show whose "Bounding Size/Aspect Ratio" is an aspect ratio
 * ("16:9") gets a size of 0x0, so Edit Transform shows 0px x 0px and the
 * slides cannot be sized or seen.
 *
 * The reworked slideshow (obs-slideshow-mk2.c) only reads a "WxH" size from
 * the setting and makes anything else 0x0, although the field is editable
 * and the original slideshow took aspect ratios too (and "Automatic").
 * slideshow_parse_size() is the parsing the source now uses:
 *
 *   - "16:9", "4:3" give the canvas widened or heightened to that ratio, as
 *     the original slideshow did;
 *   - "Automatic", an empty value, a typo or a zero give the canvas size;
 *   - control: "1280x720" is that size. */
#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <cmocka.h>

#include <stdint.h>

#include "slideshow-size.h"

#define BASE_CX 1920
#define BASE_CY 1080

static void parse(const char *str, uint32_t want_cx, uint32_t want_cy)
{
	uint32_t cx = 12345, cy = 12345;
	slideshow_parse_size(str, BASE_CX, BASE_CY, &cx, &cy);
	print_message("\"%s\" -> %ux%u (expected %ux%u)\n", str ? str : "(null)", cx, cy, want_cx, want_cy);
	assert_int_equal(cx, want_cx);
	assert_int_equal(cy, want_cy);
}

static void aspect_ratios_follow_the_canvas(void **state)
{
	(void)state;
	parse("16:9", 1920, 1080);
	parse("4:3", 1920, 1440);
	parse("16:10", 1920, 1200);
	parse("1:1", 1920, 1920);
	parse("21:9", 2520, 1080);
}

static void unusable_values_give_the_canvas_size(void **state)
{
	(void)state;
	parse("Automatic", BASE_CX, BASE_CY);
	parse("", BASE_CX, BASE_CY);
	parse(NULL, BASE_CX, BASE_CY);
	parse("big", BASE_CX, BASE_CY);
	parse("0x0", BASE_CX, BASE_CY);
	parse("1280x0", BASE_CX, BASE_CY);
	parse("0:9", BASE_CX, BASE_CY);
}

/* Control: an explicit size. */
static void explicit_size_is_kept(void **state)
{
	(void)state;
	parse("1280x720", 1280, 720);
	parse("640x480", 640, 480);
}

int main(void)
{
	const struct CMUnitTest tests[] = {
		cmocka_unit_test(aspect_ratios_follow_the_canvas),
		cmocka_unit_test(unusable_values_give_the_canvas_size),
		cmocka_unit_test(explicit_size_is_kept),
	};

	return cmocka_run_group_tests(tests, NULL, NULL);
}
