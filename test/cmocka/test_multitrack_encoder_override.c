#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <string.h>
#include <cmocka.h>

#include <multitrack-encoder-override.h>

static struct mtv_override_out translate(const char *id, const char *preset, const char *tune, bool disable_bf,
					 bool disable_lookahead)
{
	struct mtv_override_in in = {0};
	struct mtv_override_out out;

	in.preset = preset;
	in.tune = tune;
	in.disable_b_frames = disable_bf;
	in.disable_lookahead = disable_lookahead;
	mtv_translate_encoder_override(id, &in, &out);
	return out;
}

static void assert_str(const struct mtv_string_setting *slot, const char *key, const char *value)
{
	assert_non_null(slot->key);
	assert_non_null(slot->value);
	assert_string_equal(slot->key, key);
	assert_string_equal(slot->value, value);
}

static void assert_unset_str(const struct mtv_string_setting *slot)
{
	assert_null(slot->key);
}

static void test_nvenc_preset_and_tune_table(void **state)
{
	struct mtv_override_out out;

	(void)state;

	out = translate("obs_nvenc_h264_tex", "p1", "hq", false, false);
	assert_str(&out.preset, "preset", "p1");
	assert_str(&out.tune, "tune", "hq");
	assert_false(out.uhq_forces_min_bframes);

	out = translate("obs_nvenc_av1_tex", "p7", "ull", false, false);
	assert_str(&out.preset, "preset", "p7");
	assert_str(&out.tune, "tune", "ull");

	out = translate("obs_nvenc_hevc_soft", "p5", "uhq", true, false);
	assert_str(&out.preset, "preset", "p5");
	assert_str(&out.tune, "tune", "uhq");
	assert_true(out.uhq_forces_min_bframes);
	assert_int_equal(out.uhq_min_bframes, 4);
	assert_true(out.bf.set);
	assert_string_equal(out.bf.key, "bf");
	assert_int_equal(out.bf.value, 0);
	assert_false(out.bf.force);
}

static void test_legacy_nvenc_has_no_uhq(void **state)
{
	struct mtv_override_out out;

	(void)state;

	out = translate("ffmpeg_nvenc", "p3", "uhq", false, false);
	assert_str(&out.preset, "preset2", "p3");
	assert_str(&out.tune, "tune", "hq");
	assert_false(out.uhq_forces_min_bframes);

	out = translate("jim_nvenc", "p2", "ll", false, false);
	assert_str(&out.preset, "preset2", "p2");
	assert_str(&out.tune, "tune", "ll");
}

static void test_qsv_x264_and_amf_presets(void **state)
{
	struct mtv_override_out out;

	(void)state;

	out = translate("obs_qsv11_v2", "p1", NULL, false, false);
	assert_str(&out.preset, "target_usage", "TU7");
	out = translate("obs_qsv11_av1", "p7", NULL, false, false);
	assert_str(&out.preset, "target_usage", "TU1");

	out = translate("obs_x264", "p1", NULL, false, false);
	assert_str(&out.preset, "preset", "ultrafast");
	out = translate("obs_x264", "p4", NULL, false, false);
	assert_str(&out.preset, "preset", "fast");
	out = translate("obs_x264", "p7", NULL, false, false);
	assert_str(&out.preset, "preset", "veryslow");

	out = translate("h264_texture_amf", "p1", NULL, false, false);
	assert_str(&out.preset, "preset", "speed");
	out = translate("h264_fallback_amf", "p3", NULL, false, false);
	assert_str(&out.preset, "preset", "balanced");
	out = translate("h265_texture_amf", "p7", NULL, false, false);
	assert_str(&out.preset, "preset", "quality");
	out = translate("av1_texture_amf", "p7", NULL, false, false);
	assert_str(&out.preset, "preset", "highQuality");
	out = translate("av1_fallback_amf", "p5", NULL, false, false);
	assert_str(&out.preset, "preset", "quality");

	out = translate("ffmpeg_svt_av1", "p1", NULL, false, false);
	assert_null(out.preset.key);
	assert_true(out.preset_int.set);
	assert_string_equal(out.preset_int.key, "preset");
	assert_int_equal(out.preset_int.value, 12);
	out = translate("ffmpeg_svt_av1", "p7", NULL, false, false);
	assert_int_equal(out.preset_int.value, 6);
	out = translate("ffmpeg_aom_av1", "p1", NULL, false, false);
	assert_int_equal(out.preset_int.value, 10);
	out = translate("ffmpeg_aom_av1", "p7", NULL, false, false);
	assert_int_equal(out.preset_int.value, 7);
}

static void test_tune_without_an_equivalent_is_skipped(void **state)
{
	struct mtv_override_out out;

	(void)state;

	out = translate("h264_texture_amf", NULL, "ull", false, false);
	assert_unset_str(&out.tune);
	assert_unset_str(&out.latency);
	assert_true(out.tune_skipped);

	out = translate("av1_texture_amf", NULL, "uhq", false, false);
	assert_true(out.tune_skipped);
	assert_false(out.uhq_forces_min_bframes);

	out = translate("obs_x264", NULL, "hq", false, false);
	assert_unset_str(&out.tune);
	assert_true(out.tune_skipped);

	out = translate("obs_x264", NULL, "ll", false, false);
	assert_str(&out.tune, "tune", "zerolatency");
	assert_false(out.tune_skipped);

	out = translate("obs_qsv11", NULL, "hq", false, false);
	assert_str(&out.latency, "latency", "normal");
	out = translate("obs_qsv11", NULL, "ll", false, false);
	assert_str(&out.latency, "latency", "low");
	out = translate("obs_qsv11", NULL, "ull", false, false);
	assert_str(&out.latency, "latency", "ultra-low");
	out = translate("obs_qsv11", NULL, "uhq", false, false);
	assert_str(&out.latency, "latency", "normal");
	assert_false(out.uhq_forces_min_bframes);
}

static void test_unknown_encoder_keeps_service_values(void **state)
{
	struct mtv_override_out out;

	(void)state;

	out = translate("some_other_encoder", "p1", "uhq", true, true);
	assert_null(out.preset.key);
	assert_true(out.preset_skipped);
	assert_true(out.tune_skipped);
	assert_true(out.bframes_skipped);
	assert_false(out.bf.set);
	assert_false(out.lookahead.set);
	assert_false(out.pre_analysis.set);
	assert_true(out.lookahead_skipped);
	assert_true(out.strip_lookahead_opts);
	assert_false(out.uhq_forces_min_bframes);

	out = translate("some_other_encoder", "fast", "film", false, false);
	assert_true(out.preset_unknown);
	assert_true(out.tune_skipped);
}

static void test_bframes_and_lookahead_by_family(void **state)
{
	struct mtv_override_out out;

	(void)state;

	out = translate("obs_x264", NULL, NULL, true, true);
	assert_true(out.bf.set);
	assert_true(out.bf.force);
	assert_int_equal(out.bf.value, 0);
	assert_str(&out.tune, "tune", "zerolatency");
	assert_true(out.strip_lookahead_opts);

	out = translate("obs_qsv11_v2", NULL, "hq", true, true);
	assert_true(out.bf.force);
	assert_true(out.bframes.set);
	assert_string_equal(out.bframes.key, "bframes");
	assert_int_equal(out.bframes.value, 0);
	assert_str(&out.latency, "latency", "ultra-low");

	out = translate("h264_texture_amf", NULL, NULL, true, true);
	assert_true(out.bf.set);
	assert_false(out.bf.force);
	assert_true(out.pre_analysis.set);
	assert_false(out.pre_analysis.value);
	assert_string_equal(out.pre_analysis.key, "pre_analysis");

	out = translate("obs_nvenc_h264_tex", NULL, NULL, false, true);
	assert_true(out.lookahead.set);
	assert_false(out.lookahead.value);
	assert_true(out.strip_lookahead_opts);
	assert_false(out.lookahead_skipped);
}

static void expect_opts(const char *opts, bool disable, const char *expect, bool removed)
{
	char dst[128];
	bool got = mtv_filter_lookahead_opts(opts, disable, dst, sizeof(dst));

	assert_int_equal(got, removed);
	assert_string_equal(dst, expect);
}

static void test_lookahead_opts_are_stripped_only_when_disabled(void **state)
{
	const char *opts = "lookaheadDepth=4 enableLookahead=1 bitrate=6000";

	(void)state;

	expect_opts(opts, true, "bitrate=6000", true);
	expect_opts(opts, false, opts, false);
	expect_opts("rc=cbr lookaheadDepth=4", true, "rc=cbr", true);
	expect_opts("enableLookahead=0", true, "", true);
	expect_opts("lookaheadDepthExtra=1 enableLookaheadExtra=1", true,
		    "lookaheadDepthExtra=1 enableLookaheadExtra=1", false);
	expect_opts("  bitrate=1   lookaheadDepth=2  ", true, "bitrate=1", true);
	expect_opts(NULL, true, "", false);
	expect_opts("", true, "", false);

	char tiny[4];
	assert_false(mtv_filter_lookahead_opts("bitrate=6000", true, tiny, sizeof(tiny)));
	assert_string_equal(tiny, "");
}

int main(void)
{
	const struct CMUnitTest tests[] = {
		cmocka_unit_test(test_nvenc_preset_and_tune_table),
		cmocka_unit_test(test_legacy_nvenc_has_no_uhq),
		cmocka_unit_test(test_qsv_x264_and_amf_presets),
		cmocka_unit_test(test_tune_without_an_equivalent_is_skipped),
		cmocka_unit_test(test_unknown_encoder_keeps_service_values),
		cmocka_unit_test(test_bframes_and_lookahead_by_family),
		cmocka_unit_test(test_lookahead_opts_are_stripped_only_when_disabled),
	};

	return cmocka_run_group_tests(tests, NULL, NULL);
}
