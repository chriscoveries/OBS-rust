#include "multitrack-ladder.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void copy_str(char *dst, size_t size, const char *src)
{
	size_t i = 0;

	if (!size)
		return;
	if (!src)
		src = "";
	while (src[i] && i + 1 < size) {
		dst[i] = src[i];
		i++;
	}
	dst[i] = '\0';
}

static void format_value(char *dst, size_t size, const struct mtv_ladder_value *value)
{
	if (!value || value->kind == MTV_LADDER_MISSING) {
		copy_str(dst, size, "-");
		return;
	}

	switch (value->kind) {
	case MTV_LADDER_STRING:
	case MTV_LADDER_RAW:
		copy_str(dst, size, value->text ? value->text : "-");
		break;
	case MTV_LADDER_INT:
		snprintf(dst, size, "%lld", value->integer);
		break;
	case MTV_LADDER_BOOL:
		copy_str(dst, size, value->boolean ? "true" : "false");
		break;
	default:
		copy_str(dst, size, "-");
		break;
	}
}

/* Qt's QString::toLongLong accepts surrounding spaces and rejects trailing
 * junk. A numeric string keeps its original text and then gains " Kbps". */
static int parse_kbps(const char *text, long long *value)
{
	char *end = NULL;
	long long parsed;

	if (!text || !*text)
		return 0;

	errno = 0;
	parsed = strtoll(text, &end, 10);
	if (errno || end == text)
		return 0;
	while (*end == ' ' || *end == '\t')
		end++;
	if (*end != '\0')
		return 0;

	*value = parsed;
	return 1;
}

static void format_bitrate(char *dst, size_t size, const struct mtv_ladder_value *value, long long *total, int *any)
{
	long long parsed = 0;

	if (value && value->kind == MTV_LADDER_INT) {
		snprintf(dst, size, "%lld Kbps", value->integer);
		*total += value->integer;
		*any = 1;
		return;
	}

	if (value && value->kind == MTV_LADDER_STRING && parse_kbps(value->text, &parsed)) {
		snprintf(dst, size, "%s Kbps", value->text);
		*total += parsed;
		*any = 1;
		return;
	}

	format_value(dst, size, value);
}

static void format_fps(char *dst, size_t size, const struct mtv_ladder_rung *rung, const char *canvas_label)
{
	if (!rung->has_fps || rung->fps_den == 0) {
		copy_str(dst, size, canvas_label ? canvas_label : "canvas");
		return;
	}

	if (rung->fps_num % rung->fps_den == 0)
		snprintf(dst, size, "%u", rung->fps_num / rung->fps_den);
	else
		snprintf(dst, size, "%u/%u", rung->fps_num, rung->fps_den);
}

static void format_canvas(char *dst, size_t size, unsigned index, const char *const *names, size_t name_count)
{
	if (names && index < name_count && names[index])
		copy_str(dst, size, names[index]);
	else
		snprintf(dst, size, "%u", index);
}

void mtv_ladder_fill(const struct mtv_ladder_in *in, struct mtv_ladder_out *out)
{
	size_t i;

	if (!out)
		return;

	out->row_count = 0;
	out->total_bitrate_kbps = 0;
	out->any_bitrate = 0;
	out->audio_count = 0;
	out->config_id[0] = '\0';

	if (!in)
		return;

	if (!in->config_id || !in->config_id[0])
		copy_str(out->config_id, sizeof(out->config_id), "-");
	else
		copy_str(out->config_id, sizeof(out->config_id), in->config_id);

	if (in->rungs && out->rows) {
		for (i = 0; i < in->rung_count; i++) {
			const struct mtv_ladder_rung *rung = &in->rungs[i];
			struct mtv_ladder_row *row = &out->rows[i];

			snprintf(row->track, sizeof(row->track), "%u", (unsigned)i);
			format_canvas(row->canvas, sizeof(row->canvas), rung->canvas_index, in->canvas_names,
				      in->canvas_count);
			copy_str(row->encoder, sizeof(row->encoder), rung->encoder);
			snprintf(row->resolution, sizeof(row->resolution), "%ux%u", rung->width, rung->height);
			format_fps(row->fps, sizeof(row->fps), rung, in->canvas_fps_label);
			format_bitrate(row->bitrate, sizeof(row->bitrate), &rung->bitrate, &out->total_bitrate_kbps,
				       &out->any_bitrate);
			format_value(row->preset, sizeof(row->preset), &rung->preset);
			format_value(row->tune, sizeof(row->tune), &rung->tune);
			format_value(row->bframes, sizeof(row->bframes), &rung->bframes);
			format_value(row->lookahead, sizeof(row->lookahead), &rung->lookahead);
		}
		out->row_count = in->rung_count;
	}

	if (in->audio_count == 0 || (in->audio && out->audio)) {
		out->audio_count = in->audio_count;
		if (in->audio && out->audio) {
			for (i = 0; i < in->audio_count; i++) {
				copy_str(out->audio[i].codec, sizeof(out->audio[i].codec), in->audio[i].codec);
				out->audio[i].channels = in->audio[i].channels;
				format_value(out->audio[i].bitrate, sizeof(out->audio[i].bitrate),
					     &in->audio[i].bitrate);
			}
		}
	}
}
