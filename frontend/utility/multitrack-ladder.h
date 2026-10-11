#pragma once

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* One Go Live video rung, already taken out of JSON, formatted for the
 * read-only ladder table. String pointers are borrowed for the call.
 * The encoder id is the video codec, for example obs_nvenc_h264_tex. */

enum mtv_ladder_value_kind {
	MTV_LADDER_MISSING = 0,
	MTV_LADDER_STRING,
	MTV_LADDER_INT,
	MTV_LADDER_BOOL,
	MTV_LADDER_RAW,
};

struct mtv_ladder_value {
	int kind;
	const char *text;
	long long integer;
	int boolean;
};

struct mtv_ladder_rung {
	const char *encoder;
	unsigned width;
	unsigned height;
	int has_fps;
	unsigned fps_num;
	unsigned fps_den;
	unsigned canvas_index;
	struct mtv_ladder_value bitrate;
	struct mtv_ladder_value preset;
	struct mtv_ladder_value tune;
	struct mtv_ladder_value bframes;
	struct mtv_ladder_value lookahead;
};

struct mtv_ladder_audio {
	const char *codec;
	unsigned channels;
	struct mtv_ladder_value bitrate;
};

struct mtv_ladder_in {
	const struct mtv_ladder_rung *rungs;
	size_t rung_count;
	const char *const *canvas_names;
	size_t canvas_count;
	/* Shown when fps is missing or the denominator is 0. */
	const char *canvas_fps_label;
	const struct mtv_ladder_audio *audio;
	size_t audio_count;
	const char *config_id;
};

#define MTV_LADDER_CELL 160

struct mtv_ladder_row {
	char track[16];
	char canvas[MTV_LADDER_CELL];
	char encoder[MTV_LADDER_CELL];
	char resolution[32];
	char fps[32];
	char bitrate[64];
	char preset[MTV_LADDER_CELL];
	char tune[MTV_LADDER_CELL];
	char bframes[32];
	char lookahead[32];
};

struct mtv_ladder_audio_out {
	char codec[MTV_LADDER_CELL];
	unsigned channels;
	char bitrate[64];
};

struct mtv_ladder_out {
	struct mtv_ladder_row *rows;
	size_t row_count;
	long long total_bitrate_kbps;
	int any_bitrate;
	char config_id[MTV_LADDER_CELL];
	struct mtv_ladder_audio_out *audio;
	size_t audio_count;
};

/* Writes one row per rung, in that order. `rows` and `audio` need room for
 * the input counts. An empty config leaves no rows and any_bitrate false.
 * A missing config id is written as "-". */
void mtv_ladder_fill(const struct mtv_ladder_in *in, struct mtv_ladder_out *out);

#ifdef __cplusplus
}
#endif
