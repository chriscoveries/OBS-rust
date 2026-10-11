#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <math.h>
#include <stdint.h>
#include <cmocka.h>

#include <util/c99defs.h>
#include <media-io/video-io.h>
#include <media-io/frame-rate.h>

/* Characterization of the header-inline helpers in media-io/video-io.h and
 * media-io/frame-rate.h. */

/* Values just outside each enum. The enums have no negative enumerators,
 * so -1 may wrap to UINT_MAX; either way no case matches. */
static const int formats_outside[] = {-1, VIDEO_FORMAT_R10L + 1, 1000};
static const int colorspaces_outside[] = {-1, VIDEO_CS_2100_HLG + 1, 1000};
static const int ranges_outside[] = {-1, VIDEO_RANGE_FULL + 1, 1000};

#define COUNT(a) (sizeof(a) / sizeof((a)[0]))

struct format_case {
	enum video_format format;
	bool yuv;
	const char *name;
};

/* Every format, in header order. */
static const struct format_case format_cases[] = {
	{VIDEO_FORMAT_NONE, false, "None"},
	{VIDEO_FORMAT_I420, true, "I420"},
	{VIDEO_FORMAT_NV12, true, "NV12"},
	{VIDEO_FORMAT_YVYU, true, "YVYU"},
	{VIDEO_FORMAT_YUY2, true, "YUY2"},
	{VIDEO_FORMAT_UYVY, true, "UYVY"},
	{VIDEO_FORMAT_RGBA, false, "RGBA"},
	{VIDEO_FORMAT_BGRA, false, "BGRA"},
	{VIDEO_FORMAT_BGRX, false, "BGRX"},
	{VIDEO_FORMAT_Y800, false, "Y800"},
	{VIDEO_FORMAT_I444, true, "I444"},
	{VIDEO_FORMAT_BGR3, false, "BGR3"},
	{VIDEO_FORMAT_I422, true, "I422"},
	{VIDEO_FORMAT_I40A, true, "I40A"},
	{VIDEO_FORMAT_I42A, true, "I42A"},
	{VIDEO_FORMAT_YUVA, true, "YUVA"},
	{VIDEO_FORMAT_AYUV, true, "AYUV"},
	{VIDEO_FORMAT_I010, true, "I010"},
	{VIDEO_FORMAT_P010, true, "P010"},
	{VIDEO_FORMAT_I210, true, "I210"},
	{VIDEO_FORMAT_I412, true, "I412"},
	{VIDEO_FORMAT_YA2L, true, "YA2L"},
	{VIDEO_FORMAT_P216, true, "P216"},
	{VIDEO_FORMAT_P416, true, "P416"},
	/* Lower case, characterized, not endorsed. */
	{VIDEO_FORMAT_V210, true, "v210"},
	{VIDEO_FORMAT_R10L, false, "R10l"},
};

static void test_format_is_yuv(void **state)
{
	UNUSED_PARAMETER(state);

	assert_int_equal(COUNT(format_cases), VIDEO_FORMAT_R10L + 1);
	for (size_t i = 0; i < COUNT(format_cases); i++)
		assert_int_equal(format_is_yuv(format_cases[i].format), format_cases[i].yuv);
}

static void test_format_is_yuv_outside_enum(void **state)
{
	UNUSED_PARAMETER(state);

	for (size_t i = 0; i < COUNT(formats_outside); i++)
		assert_false(format_is_yuv((enum video_format)formats_outside[i]));
}

static void test_get_video_format_name(void **state)
{
	UNUSED_PARAMETER(state);

	for (size_t i = 0; i < COUNT(format_cases); i++)
		assert_string_equal(get_video_format_name(format_cases[i].format), format_cases[i].name);
}

static void test_get_video_format_name_outside_enum(void **state)
{
	UNUSED_PARAMETER(state);

	for (size_t i = 0; i < COUNT(formats_outside); i++)
		assert_string_equal(get_video_format_name((enum video_format)formats_outside[i]), "None");
}

static void test_get_video_colorspace_name(void **state)
{
	UNUSED_PARAMETER(state);

	/* DEFAULT is named as 709. */
	assert_string_equal(get_video_colorspace_name(VIDEO_CS_DEFAULT), "Rec. 709");
	assert_string_equal(get_video_colorspace_name(VIDEO_CS_601), "Rec. 601");
	assert_string_equal(get_video_colorspace_name(VIDEO_CS_709), "Rec. 709");
	assert_string_equal(get_video_colorspace_name(VIDEO_CS_SRGB), "sRGB");
	assert_string_equal(get_video_colorspace_name(VIDEO_CS_2100_PQ), "Rec. 2100 (PQ)");
	assert_string_equal(get_video_colorspace_name(VIDEO_CS_2100_HLG), "Rec. 2100 (HLG)");
}

static void test_get_video_colorspace_name_outside_enum(void **state)
{
	UNUSED_PARAMETER(state);

	for (size_t i = 0; i < COUNT(colorspaces_outside); i++)
		assert_string_equal(get_video_colorspace_name((enum video_colorspace)colorspaces_outside[i]),
				    "Unknown");
}

static void test_resolve_video_range(void **state)
{
	UNUSED_PARAMETER(state);

	/* DEFAULT resolves to partial for YUV, full otherwise; an explicit
	 * range is kept whatever the format. */
	for (size_t i = 0; i < COUNT(format_cases); i++) {
		enum video_format format = format_cases[i].format;
		enum video_range_type resolved = format_cases[i].yuv ? VIDEO_RANGE_PARTIAL : VIDEO_RANGE_FULL;

		assert_int_equal(resolve_video_range(format, VIDEO_RANGE_DEFAULT), resolved);
		assert_int_equal(resolve_video_range(format, VIDEO_RANGE_PARTIAL), VIDEO_RANGE_PARTIAL);
		assert_int_equal(resolve_video_range(format, VIDEO_RANGE_FULL), VIDEO_RANGE_FULL);
	}
}

static void test_resolve_video_range_outside_enum(void **state)
{
	UNUSED_PARAMETER(state);

	/* A format outside the enum is not YUV, so DEFAULT resolves to full. */
	for (size_t i = 0; i < COUNT(formats_outside); i++)
		assert_int_equal(resolve_video_range((enum video_format)formats_outside[i], VIDEO_RANGE_DEFAULT),
				 VIDEO_RANGE_FULL);

	/* A range outside the enum is returned unchanged. Characterized, not
	 * endorsed. */
	for (size_t i = 0; i < COUNT(ranges_outside); i++) {
		enum video_range_type range = (enum video_range_type)ranges_outside[i];
		assert_int_equal(resolve_video_range(VIDEO_FORMAT_NV12, range), range);
		assert_int_equal(resolve_video_range(VIDEO_FORMAT_RGBA, range), range);
	}
}

static void test_get_video_range_name(void **state)
{
	UNUSED_PARAMETER(state);

	for (size_t i = 0; i < COUNT(format_cases); i++) {
		enum video_format format = format_cases[i].format;

		assert_string_equal(get_video_range_name(format, VIDEO_RANGE_DEFAULT),
				    format_cases[i].yuv ? "Partial" : "Full");
		assert_string_equal(get_video_range_name(format, VIDEO_RANGE_PARTIAL), "Partial");
		assert_string_equal(get_video_range_name(format, VIDEO_RANGE_FULL), "Full");
	}
}

static void test_get_video_range_name_outside_enum(void **state)
{
	UNUSED_PARAMETER(state);

	for (size_t i = 0; i < COUNT(formats_outside); i++)
		assert_string_equal(get_video_range_name((enum video_format)formats_outside[i], VIDEO_RANGE_DEFAULT),
				    "Full");

	/* Anything that does not resolve to FULL is named partial. */
	for (size_t i = 0; i < COUNT(ranges_outside); i++) {
		enum video_range_type range = (enum video_range_type)ranges_outside[i];
		assert_string_equal(get_video_range_name(VIDEO_FORMAT_NV12, range), "Partial");
		assert_string_equal(get_video_range_name(VIDEO_FORMAT_RGBA, range), "Partial");
	}
}

static struct media_frames_per_second fps(uint32_t numerator, uint32_t denominator)
{
	struct media_frames_per_second f = {numerator, denominator};
	return f;
}

/* The conversions are single double divisions, so they are compared
 * exactly. */
static void assert_double_exact(double actual, double expected)
{
	assert_true(actual == expected);
}

static void test_frame_rate_conversions(void **state)
{
	UNUSED_PARAMETER(state);

	assert_double_exact(media_frames_per_second_to_fps(fps(30, 1)), 30.0);
	assert_double_exact(media_frames_per_second_to_frame_interval(fps(30, 1)), 1.0 / 30.0);

	assert_double_exact(media_frames_per_second_to_fps(fps(30000, 1001)), 30000.0 / 1001.0);
	assert_double_exact(media_frames_per_second_to_frame_interval(fps(30000, 1001)), 1001.0 / 30000.0);

	/* The division is in double, so the full uint32_t range is exact. */
	assert_double_exact(media_frames_per_second_to_fps(fps(UINT32_MAX, 1)), 4294967295.0);
	assert_double_exact(media_frames_per_second_to_fps(fps(UINT32_MAX, UINT32_MAX)), 1.0);
}

static void test_frame_rate_zero(void **state)
{
	UNUSED_PARAMETER(state);

	/* Zero parts are divided as is. Characterized, not endorsed. */
	assert_double_exact(media_frames_per_second_to_fps(fps(0, 1)), 0.0);
	assert_double_exact(media_frames_per_second_to_frame_interval(fps(0, 1)), INFINITY);

	assert_double_exact(media_frames_per_second_to_fps(fps(1, 0)), INFINITY);
	assert_double_exact(media_frames_per_second_to_frame_interval(fps(1, 0)), 0.0);

	assert_true(isnan(media_frames_per_second_to_fps(fps(0, 0))));
	assert_true(isnan(media_frames_per_second_to_frame_interval(fps(0, 0))));
}

static void test_frame_rate_is_valid(void **state)
{
	UNUSED_PARAMETER(state);

	assert_true(media_frames_per_second_is_valid(fps(30, 1)));
	assert_true(media_frames_per_second_is_valid(fps(30000, 1001)));
	assert_true(media_frames_per_second_is_valid(fps(UINT32_MAX, UINT32_MAX)));

	assert_false(media_frames_per_second_is_valid(fps(0, 1)));
	assert_false(media_frames_per_second_is_valid(fps(1, 0)));
	assert_false(media_frames_per_second_is_valid(fps(0, 0)));
}

int main(void)
{
	const struct CMUnitTest tests[] = {
		cmocka_unit_test(test_format_is_yuv),
		cmocka_unit_test(test_format_is_yuv_outside_enum),
		cmocka_unit_test(test_get_video_format_name),
		cmocka_unit_test(test_get_video_format_name_outside_enum),
		cmocka_unit_test(test_get_video_colorspace_name),
		cmocka_unit_test(test_get_video_colorspace_name_outside_enum),
		cmocka_unit_test(test_resolve_video_range),
		cmocka_unit_test(test_resolve_video_range_outside_enum),
		cmocka_unit_test(test_get_video_range_name),
		cmocka_unit_test(test_get_video_range_name_outside_enum),
		cmocka_unit_test(test_frame_rate_conversions),
		cmocka_unit_test(test_frame_rate_zero),
		cmocka_unit_test(test_frame_rate_is_valid),
	};

	return cmocka_run_group_tests(tests, NULL, NULL);
}
