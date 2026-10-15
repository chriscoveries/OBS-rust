/* SPDX-License-Identifier: GPL-2.0-or-later */
#define _POSIX_C_SOURCE 200809L
#include <inttypes.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <libavutil/imgutils.h>
#include <libavutil/pixdesc.h>
#include <libswscale/swscale.h>
#include "video-scaler.h"

extern int ab_before_video_scaler_create(video_scaler_t **, const struct video_scale_info *,
					 const struct video_scale_info *, enum video_scale_type);
extern void ab_before_video_scaler_destroy(video_scaler_t *);
extern bool ab_before_video_scaler_scale(video_scaler_t *, uint8_t *[], const uint32_t[], const uint8_t *const[],
					 const uint32_t[]);
extern int ab_after_video_scaler_create(video_scaler_t **, const struct video_scale_info *,
					const struct video_scale_info *, enum video_scale_type);
extern void ab_after_video_scaler_destroy(video_scaler_t *);
extern bool ab_after_video_scaler_scale(video_scaler_t *, uint8_t *[], const uint32_t[], const uint8_t *const[],
					const uint32_t[]);

static const uint8_t *before_expected_destination;
static const uint8_t *after_expected_destination;
static uint64_t before_calls, before_direct_calls, after_calls, after_direct_calls;

int ab_before_sws_scale(struct SwsContext *ctx, const uint8_t *const src[], const int src_stride[], int y, int h,
			uint8_t *const dst[], const int dst_stride[])
{
	++before_calls;
	before_direct_calls += dst[0] == before_expected_destination;
	return sws_scale(ctx, src, src_stride, y, h, dst, dst_stride);
}

int ab_after_sws_scale(struct SwsContext *ctx, const uint8_t *const src[], const int src_stride[], int y, int h,
		       uint8_t *const dst[], const int dst_stride[])
{
	++after_calls;
	after_direct_calls += dst[0] == after_expected_destination;
	return sws_scale(ctx, src, src_stride, y, h, dst, dst_stride);
}

/* The scaler source only needs allocator and logging services from libobs. */
void *bmalloc(size_t size)
{
	if (size > SIZE_MAX - 31)
		abort();
	void *p = aligned_alloc(32, (size + 31) & ~(size_t)31);
	if (!p)
		abort();
	return p;
}

void bfree(void *p)
{
	free(p);
}

int base_get_alignment(void)
{
	return 32;
}

void blog(int level, const char *format, ...)
{
	(void)level;
	va_list args;
	va_start(args, format);
	vfprintf(stderr, format, args);
	va_end(args);
	fputc('\n', stderr);
}

struct image {
	uint8_t *data[4];
	uint32_t stride[4];
	size_t extent[4];
};

struct scaler_case {
	const char *name;
	enum video_format format;
	unsigned src_scale;
	unsigned dst_scale;
};

static const struct scaler_case cases[] = {
	{"BGRA", VIDEO_FORMAT_BGRA, 1, 1}, {"BGRA", VIDEO_FORMAT_BGRA, 2, 1}, {"BGRA", VIDEO_FORMAT_BGRA, 1, 2},
	{"I420", VIDEO_FORMAT_I420, 1, 1}, {"I420", VIDEO_FORMAT_I420, 2, 1}, {"I420", VIDEO_FORMAT_I420, 1, 2},
	{"NV12", VIDEO_FORMAT_NV12, 1, 1}, {"NV12", VIDEO_FORMAT_NV12, 2, 1}, {"NV12", VIDEO_FORMAT_NV12, 1, 2},
	{"P010", VIDEO_FORMAT_P010, 1, 1}, {"P010", VIDEO_FORMAT_P010, 2, 1}, {"P010", VIDEO_FORMAT_P010, 1, 2},
};

static enum AVPixelFormat output_format(enum video_format format)
{
	switch (format) {
	case VIDEO_FORMAT_BGRA:
		return AV_PIX_FMT_BGRA;
	case VIDEO_FORMAT_I420:
		return AV_PIX_FMT_YUV420P;
	case VIDEO_FORMAT_NV12:
		return AV_PIX_FMT_NV12;
	case VIDEO_FORMAT_P010:
		return AV_PIX_FMT_P010LE;
	default:
		return AV_PIX_FMT_NONE;
	}
}

static struct image image_new(enum AVPixelFormat format, unsigned width, unsigned height)
{
	struct image image = {0};
	const AVPixFmtDescriptor *desc = av_pix_fmt_desc_get(format);
	int strides[4];
	if (!desc || av_image_fill_linesizes(strides, format, (int)width) < 0)
		abort();
	for (unsigned plane = 0; plane < 4; ++plane) {
		if (strides[plane] <= 0)
			continue;
		const unsigned shift = plane == 1 || plane == 2 ? desc->log2_chroma_h : 0;
		const unsigned rows = (height + (1u << shift) - 1) >> shift;
		image.stride[plane] = (uint32_t)strides[plane];
		image.extent[plane] = (size_t)strides[plane] * rows;
		image.data[plane] = bmalloc(image.extent[plane] + 64);
		memset(image.data[plane], 0xa5, image.extent[plane] + 64);
	}
	return image;
}

static void image_free(struct image *image)
{
	for (unsigned plane = 0; plane < 4; ++plane)
		bfree(image->data[plane]);
}

static void compare(const struct image *before, const struct image *after)
{
	for (unsigned plane = 0; plane < 4; ++plane) {
		if (!before->data[plane])
			continue;
		if (before->extent[plane] != after->extent[plane] ||
		    memcmp(before->data[plane], after->data[plane], before->extent[plane])) {
			fprintf(stderr, "pixel mismatch on plane %u\n", plane);
			exit(1);
		}
		for (unsigned i = 0; i < 64; ++i) {
			if (before->data[plane][before->extent[plane] + i] != 0xa5 ||
			    after->data[plane][after->extent[plane] + i] != 0xa5) {
				fputs("output overrun\n", stderr);
				exit(1);
			}
		}
	}
}

static bool scale_one(bool after, video_scaler_t *scaler, struct image *output, const struct image *input)
{
	if (after) {
		after_expected_destination = output->data[0];
		return ab_after_video_scaler_scale(scaler, output->data, output->stride,
						   (const uint8_t *const *)input->data, input->stride);
	}
	before_expected_destination = output->data[0];
	return ab_before_video_scaler_scale(scaler, output->data, output->stride, (const uint8_t *const *)input->data,
					    input->stride);
}

static uint64_t now_ns(void)
{
	struct timespec ts;
	if (clock_gettime(CLOCK_MONOTONIC, &ts))
		abort();
	return (uint64_t)ts.tv_sec * 1000000000u + (uint64_t)ts.tv_nsec;
}

static uint64_t scale_batch(bool after, video_scaler_t *scaler, struct image *output, const struct image *input,
			    unsigned frames)
{
	uint64_t *calls = after ? &after_calls : &before_calls;
	uint64_t *direct_calls = after ? &after_direct_calls : &before_direct_calls;
	*calls = 0;
	*direct_calls = 0;
	const uint64_t start = now_ns();
	for (unsigned frame = 0; frame < frames; ++frame) {
		if (!scale_one(after, scaler, output, input))
			exit(1);
	}
	const uint64_t elapsed = now_ns() - start;
	if (*calls != frames || *direct_calls != (after ? frames : 0)) {
		fprintf(stderr, "wrong path: version=%s calls=%" PRIu64 " direct=%" PRIu64 " expected=%u\n",
			after ? "after" : "before", *calls, *direct_calls, after ? frames : 0);
		exit(1);
	}
	return elapsed;
}

static void check_calls(bool after, unsigned expected_frames)
{
	uint64_t calls = after ? after_calls : before_calls;
	uint64_t direct = after ? after_direct_calls : before_direct_calls;
	if (calls != expected_frames || direct != (after ? expected_frames : 0)) {
		fprintf(stderr, "correctness path failed: version=%s calls=%" PRIu64 " direct=%" PRIu64 "\n",
			after ? "after" : "before", calls, direct);
		exit(1);
	}
}

static int parse_schedule(const char *text, unsigned rounds, bool schedule_ba[])
{
	if (!text || strlen(text) != (size_t)rounds * 3 - 1)
		return 0;
	for (unsigned round = 0; round < rounds; ++round) {
		const size_t offset = (size_t)round * 3;
		const char first = text[offset];
		const char second = text[offset + 1];
		if ((first != 'A' && first != 'B') || (second != 'A' && second != 'B') || first == second)
			return 0;
		if (round + 1 < rounds && text[offset + 2] != ',')
			return 0;
		schedule_ba[round] = first == 'B';
	}
	return 1;
}

static int parse_positive(const char *text, unsigned *result)
{
	char *end;
	long value = strtol(text, &end, 10);
	if (!*text || *end || value < 1 || value > 1000)
		return 0;
	*result = (unsigned)value;
	return 1;
}

int main(int argc, char **argv)
{
	bool quick = false, dry = false, measure = false;
	unsigned frames = 30, rounds = 7;
	char *schedule_text = NULL;
	for (int i = 1; i < argc; ++i) {
		if (!strcmp(argv[i], "--quick"))
			quick = true;
		else if (!strcmp(argv[i], "--dry-timing"))
			dry = true;
		else if (!strcmp(argv[i], "--measure"))
			measure = true;
		else if (!strcmp(argv[i], "--schedule") && i + 1 < argc)
			schedule_text = argv[++i];
		else if ((!strcmp(argv[i], "--frames") || !strcmp(argv[i], "--rounds")) && i + 1 < argc) {
			unsigned value;
			if (!parse_positive(argv[++i], &value))
				return 2;
			if (!strcmp(argv[i - 1], "--frames"))
				frames = value;
			else
				rounds = value;
		} else {
			fputs("usage: obs-scaler-bench --quick | --dry-timing --schedule AB | --measure --frames N --rounds N --schedule AB,BA,...\n",
			      stderr);
			return 2;
		}
	}
	if ((unsigned)quick + (unsigned)dry + (unsigned)measure != 1 || (measure && rounds < 7) ||
	    ((quick || dry) && (frames != 30 || rounds != 7)))
		return 2;
	if (dry) {
		frames = 1;
		rounds = 1;
	}
	bool schedule_ba[1000] = {0};
	if (quick) {
		if (schedule_text)
			return 2;
	} else if (!parse_schedule(schedule_text, rounds, schedule_ba)) {
		return 2;
	}
	const unsigned base_width = quick || dry ? 128 : 1920;
	const unsigned base_height = quick || dry ? 72 : 1080;
	for (size_t case_index = 0; case_index < sizeof(cases) / sizeof(cases[0]); ++case_index) {
		const struct scaler_case *item = &cases[case_index];
		struct video_scale_info src = {.format = VIDEO_FORMAT_BGRA,
					       .width = base_width * item->src_scale,
					       .height = base_height * item->src_scale,
					       .colorspace = VIDEO_CS_709,
					       .range = VIDEO_RANGE_PARTIAL};
		struct video_scale_info dst = {.format = item->format,
					       .width = base_width * item->dst_scale,
					       .height = base_height * item->dst_scale,
					       .colorspace = VIDEO_CS_709,
					       .range = VIDEO_RANGE_PARTIAL};
		video_scaler_t *before = NULL, *after = NULL;
		if (ab_before_video_scaler_create(&before, &dst, &src, VIDEO_SCALE_FAST_BILINEAR) ||
		    ab_after_video_scaler_create(&after, &dst, &src, VIDEO_SCALE_FAST_BILINEAR))
			return 1;
		struct image input = image_new(AV_PIX_FMT_BGRA, src.width, src.height);
		struct image before_output = image_new(output_format(item->format), dst.width, dst.height);
		struct image after_output = image_new(output_format(item->format), dst.width, dst.height);
		for (size_t byte = 0; byte < input.extent[0]; ++byte)
			input.data[0][byte] = byte % 4 == 3 ? 255 : (uint8_t)(byte * 31u + byte / 97u);

		/* The quick path performs no clock reads and checks every case byte-for-byte. */
		before_calls = before_direct_calls = 0;
		if (!scale_one(false, before, &before_output, &input))
			return 1;
		check_calls(false, 1);
		after_calls = after_direct_calls = 0;
		if (!scale_one(true, after, &after_output, &input))
			return 1;
		check_calls(true, 1);
		compare(&before_output, &after_output);
		printf("{\"kind\":\"correctness\",\"case\":\"%s %ux%u -> %ux%u\",\"equal\":true}\n", item->name,
		       src.width, src.height, dst.width, dst.height);
		if (quick) {
			image_free(&input);
			image_free(&before_output);
			image_free(&after_output);
			ab_before_video_scaler_destroy(before);
			ab_after_video_scaler_destroy(after);
			continue;
		}

		/* Warm both implementations before collecting any sample. */
		for (unsigned warmup = 0; warmup < (dry ? 1u : 3u); ++warmup) {
			scale_batch(false, before, &before_output, &input, frames);
			scale_batch(true, after, &after_output, &input, frames);
			compare(&before_output, &after_output);
		}
		for (unsigned round = 0; round < rounds; ++round) {
			input.data[0][0] = (uint8_t)(round * 37u + 11u);
			uint64_t before_ns, after_ns;
			const char *order = schedule_ba[round] ? "BA" : "AB";
			if (schedule_ba[round]) {
				after_ns = scale_batch(true, after, &after_output, &input, frames);
				before_ns = scale_batch(false, before, &before_output, &input, frames);
			} else {
				before_ns = scale_batch(false, before, &before_output, &input, frames);
				after_ns = scale_batch(true, after, &after_output, &input, frames);
			}
			compare(&before_output, &after_output);
			printf("{\"kind\":\"round\",\"case\":\"%s %ux%u -> %ux%u\",\"round\":%u,"
			       "\"order\":\"%s\",\"frames\":%u,\"before_ns\":%" PRIu64 ",\"after_ns\":%" PRIu64 "}\n",
			       item->name, src.width, src.height, dst.width, dst.height, round, order, frames,
			       before_ns, after_ns);
		}
		image_free(&input);
		image_free(&before_output);
		image_free(&after_output);
		ab_before_video_scaler_destroy(before);
		ab_after_video_scaler_destroy(after);
	}
	return 0;
}
