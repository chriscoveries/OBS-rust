#pragma once

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* One Enhanced Broadcasting UI choice, translated into the settings of one
 * encoder. String pointers refer to static storage. A null key means that
 * slot is unchanged, so the service value stays. */

struct mtv_string_setting {
	const char *key;
	const char *value;
};

struct mtv_int_setting {
	const char *key;
	long long value;
	bool set;
	/* Write the value even when the encoder does not publish the key.
	 * x264 reads "bf" without advertising it. */
	bool force;
};

struct mtv_bool_setting {
	const char *key;
	bool value;
	bool set;
};

struct mtv_override_in {
	/* "p1".."p7", or null/empty to keep the service preset. */
	const char *preset;
	/* "hq", "ll", "ull", "uhq", or null/empty to keep the service tune. */
	const char *tune;
	bool disable_b_frames;
	bool disable_lookahead;
};

struct mtv_override_out {
	struct mtv_string_setting preset;
	/* SVT-AV1 and AOM publish "preset" as an integer. */
	struct mtv_int_setting preset_int;
	struct mtv_string_setting tune;
	struct mtv_int_setting bf;
	struct mtv_int_setting bframes;
	struct mtv_bool_setting lookahead;
	struct mtv_bool_setting pre_analysis;
	struct mtv_string_setting latency;
	bool strip_lookahead_opts;
	/* obs-nvenc UHQ still encodes at least this many B-frames. */
	bool uhq_forces_min_bframes;
	int uhq_min_bframes;
	bool preset_unknown;
	bool preset_skipped;
	bool tune_skipped;
	bool bframes_skipped;
	bool lookahead_skipped;
};

/* Fills `out` for `encoder_id`. Unknown encoders skip every slot. */
void mtv_translate_encoder_override(const char *encoder_id, const struct mtv_override_in *in,
				    struct mtv_override_out *out);

/* Copies `opts`, dropping `lookaheadDepth=<n>` and `enableLookahead=<n>`
 * tokens when `disable_lookahead` is set. Returns whether a token was
 * removed. `dst` is always NUL-terminated when `dst_size` > 0. A too-small
 * `dst` is cleared and the function returns false. */
bool mtv_filter_lookahead_opts(const char *opts, bool disable_lookahead, char *dst, size_t dst_size);

#ifdef __cplusplus
}
#endif
