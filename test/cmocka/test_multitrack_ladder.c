#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <string.h>
#include <cmocka.h>

#include "multitrack-ladder.h"

static struct mtv_ladder_value missing_value(void)
{
	struct mtv_ladder_value value;

	memset(&value, 0, sizeof(value));
	return value;
}

static struct mtv_ladder_value string_value(const char *text)
{
	struct mtv_ladder_value value = missing_value();

	value.kind = MTV_LADDER_STRING;
	value.text = text;
	return value;
}

static struct mtv_ladder_value int_value(long long integer)
{
	struct mtv_ladder_value value = missing_value();

	value.kind = MTV_LADDER_INT;
	value.integer = integer;
	return value;
}

static struct mtv_ladder_value bool_value(int boolean)
{
	struct mtv_ladder_value value = missing_value();

	value.kind = MTV_LADDER_BOOL;
	value.boolean = boolean;
	return value;
}

static struct mtv_ladder_value raw_value(const char *text)
{
	struct mtv_ladder_value value = missing_value();

	value.kind = MTV_LADDER_RAW;
	value.text = text;
	return value;
}

static struct mtv_ladder_rung blank_rung(void)
{
	struct mtv_ladder_rung rung;

	memset(&rung, 0, sizeof(rung));
	rung.bitrate = missing_value();
	rung.preset = missing_value();
	rung.tune = missing_value();
	rung.bframes = missing_value();
	rung.lookahead = missing_value();
	return rung;
}

static void fill(const struct mtv_ladder_in *in, struct mtv_ladder_out *out, struct mtv_ladder_row *rows,
		 size_t row_count, struct mtv_ladder_audio_out *audio, size_t audio_count)
{
	memset(out, 0, sizeof(*out));
	if (rows && row_count)
		memset(rows, 0, row_count * sizeof(*rows));
	if (audio && audio_count)
		memset(audio, 0, audio_count * sizeof(*audio));
	out->rows = rows;
	out->audio = audio;
	mtv_ladder_fill(in, out);
}

static void test_rungs_keep_order_resolution_and_codec(void **state)
{
	struct mtv_ladder_rung rungs[2];
	struct mtv_ladder_row rows[2];
	struct mtv_ladder_out out;
	const char *canvases[] = {"Main", "Vertical"};
	struct mtv_ladder_in in;

	(void)state;
	rungs[0] = blank_rung();
	rungs[0].encoder = "obs_nvenc_h264_tex";
	rungs[0].width = 1720;
	rungs[0].height = 720;
	rungs[0].canvas_index = 0;
	rungs[1] = blank_rung();
	rungs[1].encoder = "obs_nvenc_hevc_tex";
	rungs[1].width = 380;
	rungs[1].height = 160;
	rungs[1].canvas_index = 1;

	memset(&in, 0, sizeof(in));
	in.rungs = rungs;
	in.rung_count = 2;
	in.canvas_names = canvases;
	in.canvas_count = 2;
	in.canvas_fps_label = "canvas";
	fill(&in, &out, rows, 2, NULL, 0);

	assert_int_equal(out.row_count, 2);
	assert_string_equal(rows[0].track, "0");
	assert_string_equal(rows[0].canvas, "Main");
	assert_string_equal(rows[0].encoder, "obs_nvenc_h264_tex");
	assert_string_equal(rows[0].resolution, "1720x720");
	assert_string_equal(rows[1].track, "1");
	assert_string_equal(rows[1].canvas, "Vertical");
	assert_string_equal(rows[1].encoder, "obs_nvenc_hevc_tex");
	assert_string_equal(rows[1].resolution, "380x160");
}

static void test_fps_and_bitrate(void **state)
{
	struct mtv_ladder_rung rungs[4];
	struct mtv_ladder_row rows[4];
	struct mtv_ladder_out out;
	struct mtv_ladder_in in;

	(void)state;
	rungs[0] = blank_rung();
	rungs[0].width = 1280;
	rungs[0].height = 720;
	rungs[0].has_fps = 1;
	rungs[0].fps_num = 60;
	rungs[0].fps_den = 1;
	rungs[0].bitrate = int_value(6000);

	rungs[1] = blank_rung();
	rungs[1].has_fps = 1;
	rungs[1].fps_num = 30000;
	rungs[1].fps_den = 1001;
	rungs[1].bitrate = string_value("2500");

	rungs[2] = blank_rung();
	rungs[2].has_fps = 0;
	rungs[2].bitrate = missing_value();

	rungs[3] = blank_rung();
	rungs[3].has_fps = 1;
	rungs[3].fps_num = 30;
	rungs[3].fps_den = 0;
	rungs[3].bitrate = string_value("fast");

	memset(&in, 0, sizeof(in));
	in.rungs = rungs;
	in.rung_count = 4;
	in.canvas_fps_label = "canvas";
	fill(&in, &out, rows, 4, NULL, 0);

	assert_string_equal(rows[0].fps, "60");
	assert_string_equal(rows[0].bitrate, "6000 Kbps");
	assert_string_equal(rows[1].fps, "30000/1001");
	assert_string_equal(rows[1].bitrate, "2500 Kbps");
	assert_string_equal(rows[2].fps, "canvas");
	assert_string_equal(rows[2].bitrate, "-");
	assert_string_equal(rows[3].fps, "canvas");
	assert_string_equal(rows[3].bitrate, "fast");
	assert_true(out.any_bitrate);
	assert_int_equal(out.total_bitrate_kbps, 8500);
}

static void test_missing_or_invalid_fields(void **state)
{
	struct mtv_ladder_rung rung;
	struct mtv_ladder_row row;
	struct mtv_ladder_out out;
	const char *canvases[] = {"Main"};
	struct mtv_ladder_in in;

	(void)state;
	rung = blank_rung();
	rung.encoder = NULL;
	rung.width = 0;
	rung.height = 0;
	rung.canvas_index = 9;
	rung.preset = string_value("p5");
	rung.tune = raw_value("{}");
	rung.bframes = int_value(0);
	rung.lookahead = bool_value(0);

	memset(&in, 0, sizeof(in));
	in.rungs = &rung;
	in.rung_count = 1;
	in.canvas_names = canvases;
	in.canvas_count = 1;
	in.canvas_fps_label = "canvas";
	fill(&in, &out, &row, 1, NULL, 0);

	assert_string_equal(row.encoder, "");
	assert_string_equal(row.resolution, "0x0");
	assert_string_equal(row.canvas, "9");
	assert_string_equal(row.preset, "p5");
	assert_string_equal(row.tune, "{}");
	assert_string_equal(row.bframes, "0");
	assert_string_equal(row.lookahead, "false");
	assert_string_equal(row.fps, "canvas");
	assert_string_equal(row.bitrate, "-");
	assert_false(out.any_bitrate);
}

static void test_empty_config(void **state)
{
	struct mtv_ladder_out out;
	struct mtv_ladder_audio audio;
	struct mtv_ladder_audio_out audio_out;
	struct mtv_ladder_in in;

	(void)state;
	memset(&in, 0, sizeof(in));
	in.canvas_fps_label = "canvas";
	fill(&in, &out, NULL, 0, NULL, 0);

	assert_int_equal(out.row_count, 0);
	assert_false(out.any_bitrate);
	assert_int_equal(out.total_bitrate_kbps, 0);
	assert_string_equal(out.config_id, "-");
	assert_int_equal(out.audio_count, 0);

	audio.codec = "aac";
	audio.channels = 2;
	audio.bitrate = int_value(160);
	memset(&in, 0, sizeof(in));
	in.audio = &audio;
	in.audio_count = 1;
	in.config_id = "9f961963-bc10-4229-9b03-43d1fef2afd3";
	fill(&in, &out, NULL, 0, &audio_out, 1);

	assert_string_equal(out.config_id, "9f961963-bc10-4229-9b03-43d1fef2afd3");
	assert_int_equal(out.audio_count, 1);
	assert_string_equal(audio_out.codec, "aac");
	assert_int_equal(audio_out.channels, 2);
	assert_string_equal(audio_out.bitrate, "160");
}

int main(void)
{
	const struct CMUnitTest tests[] = {
		cmocka_unit_test(test_rungs_keep_order_resolution_and_codec),
		cmocka_unit_test(test_fps_and_bitrate),
		cmocka_unit_test(test_missing_or_invalid_fields),
		cmocka_unit_test(test_empty_config),
	};

	return cmocka_run_group_tests(tests, NULL, NULL);
}
