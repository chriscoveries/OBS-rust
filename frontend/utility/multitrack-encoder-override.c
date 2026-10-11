#include "multitrack-encoder-override.h"

#include <string.h>

enum mtv_family {
	MTV_OTHER = 0,
	MTV_NVENC,
	MTV_NVENC_LEGACY,
	MTV_QSV,
	MTV_AMF,
	MTV_AMF_AV1,
	MTV_X264,
	MTV_SVT,
	MTV_AOM,
};

static bool contains(const char *s, const char *part)
{
	return s && part && strstr(s, part) != NULL;
}

static enum mtv_family family_from_id(const char *id)
{
	if (!id || !*id)
		return MTV_OTHER;
	if (contains(id, "qsv"))
		return MTV_QSV;
	if (contains(id, "amf"))
		return contains(id, "av1") ? MTV_AMF_AV1 : MTV_AMF;
	if (contains(id, "x264"))
		return MTV_X264;
	if (contains(id, "svt"))
		return MTV_SVT;
	if (contains(id, "aom"))
		return MTV_AOM;
	if (contains(id, "nvenc"))
		return strncmp(id, "obs_nvenc", 9) == 0 ? MTV_NVENC : MTV_NVENC_LEGACY;
	return MTV_OTHER;
}

/* UI presets are p1 (fastest) through p7 (slowest). */
static int preset_level(const char *choice)
{
	if (!choice || choice[0] != 'p' || choice[1] < '1' || choice[1] > '7' || choice[2] != '\0')
		return -1;
	return choice[1] - '1';
}

static bool tune_is_known(const char *tune)
{
	return tune && (strcmp(tune, "hq") == 0 || strcmp(tune, "ll") == 0 || strcmp(tune, "ull") == 0 ||
			strcmp(tune, "uhq") == 0);
}

static void set_str(struct mtv_string_setting *slot, const char *key, const char *value)
{
	slot->key = key;
	slot->value = value;
}

static void set_int(struct mtv_int_setting *slot, const char *key, long long value, bool force)
{
	slot->key = key;
	slot->value = value;
	slot->set = true;
	slot->force = force;
}

static void set_bool(struct mtv_bool_setting *slot, const char *key, bool value)
{
	slot->key = key;
	slot->value = value;
	slot->set = true;
}

void mtv_translate_encoder_override(const char *encoder_id, const struct mtv_override_in *in,
				    struct mtv_override_out *out)
{
	static const char *const x264_presets[] = {"ultrafast", "superfast", "veryfast", "fast",
						   "medium",    "slow",      "veryslow"};
	static const char *const qsv_presets[] = {"TU7", "TU6", "TU5", "TU4", "TU3", "TU2", "TU1"};
	static const char *const amf_presets[] = {"speed",   "speed",   "balanced", "balanced",
						  "quality", "quality", "quality"};
	static const int svt_presets[] = {12, 11, 10, 9, 8, 7, 6};
	static const int aom_presets[] = {10, 10, 9, 9, 8, 8, 7};

	const enum mtv_family family = family_from_id(encoder_id);
	const char *preset = in && in->preset ? in->preset : "";
	const char *tune = in && in->tune ? in->tune : "";
	const bool disable_bf = in && in->disable_b_frames;
	const bool disable_lookahead = in && in->disable_lookahead;
	const int level = preset_level(preset);

	memset(out, 0, sizeof(*out));
	out->uhq_min_bframes = 4;

	if (preset[0]) {
		if (level < 0) {
			out->preset_unknown = true;
		} else if (family == MTV_NVENC) {
			set_str(&out->preset, "preset", preset);
		} else if (family == MTV_NVENC_LEGACY) {
			set_str(&out->preset, "preset2", preset);
		} else if (family == MTV_X264) {
			set_str(&out->preset, "preset", x264_presets[level]);
		} else if (family == MTV_QSV) {
			set_str(&out->preset, "target_usage", qsv_presets[level]);
		} else if (family == MTV_AMF_AV1 && level == 6) {
			set_str(&out->preset, "preset", "highQuality");
		} else if (family == MTV_AMF || family == MTV_AMF_AV1) {
			set_str(&out->preset, "preset", amf_presets[level]);
		} else if (family == MTV_SVT) {
			set_int(&out->preset_int, "preset", svt_presets[level], false);
		} else if (family == MTV_AOM) {
			set_int(&out->preset_int, "preset", aom_presets[level], false);
		} else {
			out->preset_skipped = true;
		}
	}

	if (tune[0]) {
		if (!tune_is_known(tune)) {
			out->tune_skipped = true;
		} else if (family == MTV_NVENC) {
			set_str(&out->tune, "tune", tune);
			if (strcmp(tune, "uhq") == 0)
				out->uhq_forces_min_bframes = true;
		} else if (family == MTV_NVENC_LEGACY) {
			/* FFmpeg NVENC and the compat ids stop at hq. */
			set_str(&out->tune, "tune", strcmp(tune, "uhq") == 0 ? "hq" : tune);
		} else if (family == MTV_QSV) {
			const char *latency = "normal";
			if (strcmp(tune, "ull") == 0)
				latency = "ultra-low";
			else if (strcmp(tune, "ll") == 0)
				latency = "low";
			set_str(&out->latency, "latency", latency);
		} else if (family == MTV_X264 && (strcmp(tune, "ll") == 0 || strcmp(tune, "ull") == 0)) {
			set_str(&out->tune, "tune", "zerolatency");
		} else {
			out->tune_skipped = true;
		}
	}

	if (disable_bf) {
		if (family == MTV_NVENC || family == MTV_NVENC_LEGACY || family == MTV_AMF || family == MTV_AMF_AV1) {
			set_int(&out->bf, "bf", 0, false);
		} else if (family == MTV_QSV) {
			set_int(&out->bf, "bf", 0, true);
			set_int(&out->bframes, "bframes", 0, false);
		} else if (family == MTV_X264) {
			set_int(&out->bf, "bf", 0, true);
		} else {
			out->bframes_skipped = true;
		}
	}

	if (disable_lookahead) {
		bool applied = false;
		if (family == MTV_NVENC || family == MTV_NVENC_LEGACY) {
			set_bool(&out->lookahead, "lookahead", false);
			applied = true;
		}
		if (family == MTV_AMF || family == MTV_AMF_AV1) {
			set_bool(&out->pre_analysis, "pre_analysis", false);
			applied = true;
		}
		if (family == MTV_QSV) {
			set_str(&out->latency, "latency", "ultra-low");
			applied = true;
		}
		if (family == MTV_X264) {
			set_str(&out->tune, "tune", "zerolatency");
			applied = true;
		}
		/* Any encoder can carry Go Live opts. Stripping them counts
		 * as applying the override when the settings object has opts. */
		out->strip_lookahead_opts = true;
		if (!applied)
			out->lookahead_skipped = true;
	}
}

static bool token_is_opt(const char *token, size_t len, const char *name)
{
	const size_t name_len = strlen(name);
	if (len <= name_len || token[name_len] != '=')
		return false;
	return memcmp(token, name, name_len) == 0;
}

bool mtv_filter_lookahead_opts(const char *opts, bool disable_lookahead, char *dst, size_t dst_size)
{
	const char *p;
	size_t used = 0;
	bool removed = false;

	if (!dst || dst_size == 0)
		return false;
	if (!opts)
		opts = "";

	if (!disable_lookahead) {
		const size_t n = strlen(opts);
		if (n + 1 > dst_size) {
			dst[0] = '\0';
			return false;
		}
		memcpy(dst, opts, n + 1);
		return false;
	}

	for (p = opts; *p;) {
		const char *start;
		size_t len;
		size_t extra;

		while (*p == ' ')
			p++;
		if (!*p)
			break;
		start = p;
		while (*p && *p != ' ')
			p++;
		len = (size_t)(p - start);
		if (token_is_opt(start, len, "lookaheadDepth") || token_is_opt(start, len, "enableLookahead")) {
			removed = true;
			continue;
		}

		extra = len + (used ? 1 : 0);
		if (used + extra + 1 > dst_size) {
			dst[0] = '\0';
			return false;
		}
		if (used)
			dst[used++] = ' ';
		memcpy(dst + used, start, len);
		used += len;
	}

	dst[used] = '\0';
	return removed;
}
