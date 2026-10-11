#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <stdint.h>
#include <cmocka.h>

#include <util/c99defs.h>
#include <media-io/audio-io.h>

/* Characterization of the header-inline helpers in media-io/audio-io.h. */

/* Values just outside each enum. The enums have no negative enumerators,
 * so -1 may wrap to UINT_MAX; either way no case matches. speaker_layout
 * skips 7 (SPEAKERS_7POINT1 is 8), so 7 is outside it too. */
static const int formats_outside[] = {-1, AUDIO_FORMAT_FLOAT_PLANAR + 1, 1000};
static const int layouts_outside[] = {-1, 7, SPEAKERS_7POINT1 + 1, 1000};

#define COUNT(a) (sizeof(a) / sizeof((a)[0]))

struct format_case {
	enum audio_format format;
	size_t bytes_per_channel;
	bool planar;
};

/* Every format, in header order. */
static const struct format_case format_cases[] = {
	{AUDIO_FORMAT_UNKNOWN, 0, false},     {AUDIO_FORMAT_U8BIT, 1, false},
	{AUDIO_FORMAT_16BIT, 2, false},       {AUDIO_FORMAT_32BIT, 4, false},
	{AUDIO_FORMAT_FLOAT, 4, false},       {AUDIO_FORMAT_U8BIT_PLANAR, 1, true},
	{AUDIO_FORMAT_16BIT_PLANAR, 2, true}, {AUDIO_FORMAT_32BIT_PLANAR, 4, true},
	{AUDIO_FORMAT_FLOAT_PLANAR, 4, true},
};

struct layout_case {
	enum speaker_layout speakers;
	uint32_t channels;
};

/* Every layout, in header order. */
static const struct layout_case layout_cases[] = {
	{SPEAKERS_UNKNOWN, 0}, {SPEAKERS_MONO, 1},    {SPEAKERS_STEREO, 2},  {SPEAKERS_2POINT1, 3},
	{SPEAKERS_4POINT0, 4}, {SPEAKERS_4POINT1, 5}, {SPEAKERS_5POINT1, 6}, {SPEAKERS_7POINT1, 8},
};

static void test_get_audio_channels(void **state)
{
	UNUSED_PARAMETER(state);

	for (size_t i = 0; i < COUNT(layout_cases); i++)
		assert_int_equal(get_audio_channels(layout_cases[i].speakers), layout_cases[i].channels);
}

static void test_get_audio_channels_outside_enum(void **state)
{
	UNUSED_PARAMETER(state);

	/* No case matches: 0 channels, like SPEAKERS_UNKNOWN. */
	for (size_t i = 0; i < COUNT(layouts_outside); i++)
		assert_int_equal(get_audio_channels((enum speaker_layout)layouts_outside[i]), 0);
}

static void test_get_audio_bytes_per_channel(void **state)
{
	UNUSED_PARAMETER(state);

	for (size_t i = 0; i < COUNT(format_cases); i++)
		assert_int_equal(get_audio_bytes_per_channel(format_cases[i].format),
				 format_cases[i].bytes_per_channel);
}

static void test_is_audio_planar(void **state)
{
	UNUSED_PARAMETER(state);

	for (size_t i = 0; i < COUNT(format_cases); i++)
		assert_int_equal(is_audio_planar(format_cases[i].format), format_cases[i].planar);
}

static void test_format_helpers_outside_enum(void **state)
{
	UNUSED_PARAMETER(state);

	/* No case matches: 0 bytes and interleaved, like AUDIO_FORMAT_UNKNOWN. */
	for (size_t i = 0; i < COUNT(formats_outside); i++) {
		enum audio_format format = (enum audio_format)formats_outside[i];
		assert_int_equal(get_audio_bytes_per_channel(format), 0);
		assert_false(is_audio_planar(format));
	}
}

static void test_get_audio_planes(void **state)
{
	UNUSED_PARAMETER(state);

	/* Planar formats have one plane per channel, interleaved ones a single
	 * plane, even with no channels. */
	for (size_t f = 0; f < COUNT(format_cases); f++) {
		for (size_t s = 0; s < COUNT(layout_cases); s++) {
			size_t planes = format_cases[f].planar ? layout_cases[s].channels : 1;
			assert_int_equal(get_audio_planes(format_cases[f].format, layout_cases[s].speakers), planes);
		}
	}

	/* Unknown values: an unknown format is interleaved, an unknown layout
	 * has no channels. */
	assert_int_equal(get_audio_planes((enum audio_format)1000, SPEAKERS_5POINT1), 1);
	assert_int_equal(get_audio_planes(AUDIO_FORMAT_FLOAT_PLANAR, (enum speaker_layout)7), 0);
}

static void test_get_audio_size(void **state)
{
	UNUSED_PARAMETER(state);

	/* Bytes per plane: interleaved holds every channel, planar one. */
	assert_int_equal(get_audio_size(AUDIO_FORMAT_FLOAT, SPEAKERS_STEREO, 1), 8);
	assert_int_equal(get_audio_size(AUDIO_FORMAT_16BIT_PLANAR, SPEAKERS_5POINT1, 1), 2);
	assert_int_equal(get_audio_size(AUDIO_FORMAT_FLOAT, SPEAKERS_STEREO, AUDIO_OUTPUT_FRAMES), 8192);
	assert_int_equal(get_audio_size(AUDIO_FORMAT_FLOAT_PLANAR, SPEAKERS_7POINT1, AUDIO_OUTPUT_FRAMES), 4096);
	assert_int_equal(get_audio_size(AUDIO_FORMAT_U8BIT, SPEAKERS_MONO, 480), 480);
	assert_int_equal(get_audio_size(AUDIO_FORMAT_32BIT, SPEAKERS_7POINT1, UINT32_MAX), (size_t)32 * UINT32_MAX);

	/* Zero frames, an unknown format or (interleaved) an unknown layout. */
	assert_int_equal(get_audio_size(AUDIO_FORMAT_FLOAT, SPEAKERS_STEREO, 0), 0);
	assert_int_equal(get_audio_size(AUDIO_FORMAT_UNKNOWN, SPEAKERS_STEREO, 1024), 0);
	assert_int_equal(get_audio_size(AUDIO_FORMAT_FLOAT, SPEAKERS_UNKNOWN, 1024), 0);
	/* Planar ignores the layout, even an unknown one. */
	assert_int_equal(get_audio_size(AUDIO_FORMAT_FLOAT_PLANAR, SPEAKERS_UNKNOWN, 1024), 4096);
	assert_int_equal(get_audio_size(AUDIO_FORMAT_FLOAT_PLANAR, (enum speaker_layout)7, 1024), 4096);
}

static void test_get_total_audio_size(void **state)
{
	UNUSED_PARAMETER(state);

	/* Bytes across all planes, planar or not. */
	assert_int_equal(get_total_audio_size(AUDIO_FORMAT_FLOAT, SPEAKERS_STEREO, 1), 8);
	assert_int_equal(get_total_audio_size(AUDIO_FORMAT_16BIT_PLANAR, SPEAKERS_5POINT1, 1), 12);
	assert_int_equal(get_total_audio_size(AUDIO_FORMAT_FLOAT_PLANAR, SPEAKERS_7POINT1, AUDIO_OUTPUT_FRAMES), 32768);
	assert_int_equal(get_total_audio_size(AUDIO_FORMAT_32BIT_PLANAR, SPEAKERS_7POINT1, UINT32_MAX),
			 (size_t)32 * UINT32_MAX);

	assert_int_equal(get_total_audio_size(AUDIO_FORMAT_FLOAT, SPEAKERS_STEREO, 0), 0);
	assert_int_equal(get_total_audio_size(AUDIO_FORMAT_UNKNOWN, SPEAKERS_STEREO, 1024), 0);
	assert_int_equal(get_total_audio_size(AUDIO_FORMAT_FLOAT_PLANAR, SPEAKERS_UNKNOWN, 1024), 0);
	assert_int_equal(get_total_audio_size((enum audio_format)1000, (enum speaker_layout)7, 1024), 0);

	/* TOTAL_AUDIO_SIZE is every mix at the largest layout, in float. */
	assert_int_equal(TOTAL_AUDIO_SIZE, MAX_AUDIO_MIXES * get_total_audio_size(AUDIO_FORMAT_FLOAT, SPEAKERS_7POINT1,
										  AUDIO_OUTPUT_FRAMES));
}

static void test_audio_frames_to_ns(void **state)
{
	UNUSED_PARAMETER(state);

	assert_int_equal(audio_frames_to_ns(48000, 48000), 1000000000ULL);
	assert_int_equal(audio_frames_to_ns(44100, 44100), 1000000000ULL);
	assert_int_equal(audio_frames_to_ns(48000, 0), 0);
	/* Truncated: 1024 frames is 21333333.3 ns at 48 kHz, 23219954.6 at
	 * 44.1 kHz. */
	assert_int_equal(audio_frames_to_ns(48000, AUDIO_OUTPUT_FRAMES), 21333333ULL);
	assert_int_equal(audio_frames_to_ns(44100, AUDIO_OUTPUT_FRAMES), 23219954ULL);
	assert_int_equal(audio_frames_to_ns(1, 1), 1000000000ULL);

	/* frames * 1e9 is past 64 bits, the result is not, and the remainder
	 * times 1e9 fits, so both util_mul_div64 paths agree. Larger results
	 * differ per platform (see test_util_uint64.c); sample_rate 0 is a
	 * division by zero. */
	assert_int_equal(audio_frames_to_ns(48000, 885443715504000ULL), 18446744073000000000ULL);
}

static void test_ns_to_audio_frames(void **state)
{
	UNUSED_PARAMETER(state);

	assert_int_equal(ns_to_audio_frames(48000, 1000000000ULL), 48000);
	assert_int_equal(ns_to_audio_frames(44100, 1000000000ULL), 44100);
	assert_int_equal(ns_to_audio_frames(48000, 0), 0);
	/* Truncated, so a round trip can lose a frame. */
	assert_int_equal(ns_to_audio_frames(48000, 21333333ULL), 1023);
	assert_int_equal(audio_frames_to_ns(44100, 1), 22675ULL);
	assert_int_equal(ns_to_audio_frames(44100, audio_frames_to_ns(44100, 1)), 0);
	assert_int_equal(ns_to_audio_frames(48000, 20833333ULL), 999);
	/* The rate is the multiplier, so 0 is allowed: no frames. */
	assert_int_equal(ns_to_audio_frames(0, 1000000000ULL), 0);

	/* ns * 48000 is past 64 bits, the result is not, and the remainder
	 * times 48000 fits, so both util_mul_div64 paths agree. */
	assert_int_equal(ns_to_audio_frames(48000, UINT64_MAX), 885443715538058ULL);
}

int main(void)
{
	const struct CMUnitTest tests[] = {
		cmocka_unit_test(test_get_audio_channels),
		cmocka_unit_test(test_get_audio_channels_outside_enum),
		cmocka_unit_test(test_get_audio_bytes_per_channel),
		cmocka_unit_test(test_is_audio_planar),
		cmocka_unit_test(test_format_helpers_outside_enum),
		cmocka_unit_test(test_get_audio_planes),
		cmocka_unit_test(test_get_audio_size),
		cmocka_unit_test(test_get_total_audio_size),
		cmocka_unit_test(test_audio_frames_to_ns),
		cmocka_unit_test(test_ns_to_audio_frames),
	};

	return cmocka_run_group_tests(tests, NULL, NULL);
}
