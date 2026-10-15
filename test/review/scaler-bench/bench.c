/* SPDX-License-Identifier: GPL-2.0-or-later */
#define _POSIX_C_SOURCE 200809L
#include <inttypes.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <libswscale/swscale.h>

/* Observe the destination supplied by the actual production code. */
static const uint8_t *expected_destination;
static uint64_t calls, direct_calls;
static int counted_scale(struct SwsContext *ctx, const uint8_t *const src[], const int src_stride[], int y, int h,
			 uint8_t *const dst[], const int dst_stride[])
{
	++calls;
	direct_calls += dst[0] == expected_destination;
	return sws_scale(ctx, src, src_stride, y, h, dst, dst_stride);
}
#define sws_scale counted_scale
#include "../../../libobs/media-io/video-scaler-ffmpeg.c"
#undef sws_scale

/* Only allocation/logging are supplied locally; the scaler and FFmpeg are real. */
void *bmalloc(size_t size)
{
	if (size > SIZE_MAX - 31)
		abort();
	void *p = aligned_alloc(32, (size + 31) & ~(size_t)31);
	if (!p)
		abort();
	return p;
}
void bfree(void *p) { free(p); }
int base_get_alignment(void) { return 32; }
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

static struct image image_new(enum video_format format, unsigned w, unsigned h)
{
	struct image image = {0};
	const enum AVPixelFormat fmt = get_ffmpeg_video_format(format);
	const AVPixFmtDescriptor *desc = av_pix_fmt_desc_get(fmt);
	int strides[4];
	if (!desc || av_image_fill_linesizes(strides, fmt, (int)w) < 0)
		abort();
	for (unsigned p = 0; p < 4; ++p) {
		if (strides[p] <= 0)
			continue;
		const unsigned shift = p == 1 || p == 2 ? desc->log2_chroma_h : 0;
		const unsigned rows = (h + (1u << shift) - 1) >> shift;
		image.stride[p] = (uint32_t)strides[p];
		image.extent[p] = (size_t)strides[p] * rows;
		image.data[p] = bmalloc(image.extent[p] + 64);
		memset(image.data[p], 0xa5, image.extent[p] + 64);
	}
	return image;
}

static void image_free(struct image *image)
{
	for (unsigned p = 0; p < 4; ++p)
		bfree(image->data[p]);
}

static void compare(const struct image *a, const struct image *b)
{
	for (unsigned p = 0; p < 4; ++p) {
		if (!a->data[p])
			continue;
		if (a->extent[p] != b->extent[p] || memcmp(a->data[p], b->data[p], a->extent[p])) {
			fprintf(stderr, "pixel mismatch on plane %u\n", p);
			exit(1);
		}
		for (unsigned i = 0; i < 64; ++i) {
			if (a->data[p][a->extent[p] + i] != 0xa5 || b->data[p][b->extent[p] + i] != 0xa5) {
				fputs("output overrun\n", stderr);
				exit(1);
			}
		}
	}
}

static uint64_t now_ns(void)
{
	struct timespec ts;
	if (clock_gettime(CLOCK_MONOTONIC, &ts))
		abort();
	return (uint64_t)ts.tv_sec * 1000000000u + (uint64_t)ts.tv_nsec;
}

static uint64_t run(video_scaler_t *scaler, struct image *out, const struct image *in, unsigned frames, bool direct)
{
	expected_destination = out->data[0];
	calls = direct_calls = 0;
	const uint64_t start = now_ns();
	for (unsigned i = 0; i < frames; ++i) {
		if (!video_scaler_scale(scaler, out->data, out->stride, (const uint8_t *const *)in->data, in->stride))
			exit(1);
	}
	const uint64_t elapsed = now_ns() - start;
	if (calls != frames || direct_calls != (direct ? frames : 0)) {
		fprintf(stderr, "wrong path: total=%" PRIu64 " direct=%" PRIu64 " expected=%u\n", calls, direct_calls,
			direct ? frames : 0);
		exit(1);
	}
	return elapsed;
}

int main(int argc, char **argv)
{
	bool quick = false;
	unsigned frames = 30;
	if (argc == 2 && !strcmp(argv[1], "--quick")) {
		quick = true;
		frames = 1;
	} else if (argc == 3 && !strcmp(argv[1], "--frames")) {
		char *end;
		const long n = strtol(argv[2], &end, 10);
		if (!*argv[2] || *end || n < 1 || n > 1000)
			return 2;
		frames = (unsigned)n;
	} else if (argc != 1) {
		fputs("usage: obs-scaler-bench [--quick | --frames 1..1000]\n", stderr);
		return 2;
	}
	const enum video_format formats[] = {VIDEO_FORMAT_BGRA, VIDEO_FORMAT_I420, VIDEO_FORMAT_NV12, VIDEO_FORMAT_P010};
	const char *names[] = {"BGRA", "I420", "NV12", "P010"};
	const unsigned w = quick ? 128 : 1920, h = quick ? 72 : 1080;
	printf("{\"kind\":\"environment\",\"swscale_version\":%u,\"avutil_version\":%u}\n", swscale_version(), avutil_version());
	for (unsigned f = 0; f < 4; ++f) {
		for (unsigned geometry = 0; geometry < 3; ++geometry) {
			struct video_scale_info src = {.format = VIDEO_FORMAT_BGRA, .width = w * (geometry == 1 ? 2 : 1),
				.height = h * (geometry == 1 ? 2 : 1), .colorspace = VIDEO_CS_709, .range = VIDEO_RANGE_PARTIAL};
			struct video_scale_info dst = {.format = formats[f], .width = w * (geometry == 2 ? 2 : 1),
				.height = h * (geometry == 2 ? 2 : 1), .colorspace = VIDEO_CS_709, .range = VIDEO_RANGE_PARTIAL};
			video_scaler_t *copy = NULL, *direct = NULL;
			if (video_scaler_create(&copy, &dst, &src, VIDEO_SCALE_FAST_BILINEAR) ||
			    video_scaler_create(&direct, &dst, &src, VIDEO_SCALE_FAST_BILINEAR))
				return 1;
			/* Private benchmark-only instance: exactly the existing scratch path. */
			copy->direct_height_ok = false;
			struct image input = image_new(src.format, src.width, src.height);
			struct image a = image_new(dst.format, dst.width, dst.height), b = image_new(dst.format, dst.width, dst.height);
			for (size_t i = 0; i < input.extent[0]; ++i)
				input.data[0][i] = i % 4 == 3 ? 255 : (uint8_t)(i * 31u + i / 97u);
			run(copy, &a, &input, 3, false);
			run(direct, &b, &input, 3, true);
			compare(&a, &b);
			for (unsigned pair = 0; pair < 8; ++pair) {
				input.data[0][0] = (uint8_t)(pair * 29u);
				uint64_t copy_ns, direct_ns;
				if (pair % 2 == 0) {
					copy_ns = run(copy, &a, &input, frames, false);
					direct_ns = run(direct, &b, &input, frames, true);
				} else {
					direct_ns = run(direct, &b, &input, frames, true);
					copy_ns = run(copy, &a, &input, frames, false);
				}
				compare(&a, &b);
				printf("{\"kind\":\"pair\",\"format\":\"%s\",\"src_w\":%u,\"src_h\":%u,\"dst_w\":%u,\"dst_h\":%u,"
				       "\"pair\":%u,\"frames\":%u,\"order\":\"%s\",\"copy_calls\":%u,\"direct_calls\":%u,"
				       "\"copy_ns\":%" PRIu64 ",\"direct_ns\":%" PRIu64 ",\"equal\":true,\"quick\":%s}\n",
				       names[f], src.width, src.height, dst.width, dst.height, pair, frames, pair % 2 ? "DC" : "CD",
				       frames, frames, copy_ns, direct_ns, quick ? "true" : "false");
			}
			image_free(&input); image_free(&a); image_free(&b);
			video_scaler_destroy(copy); video_scaler_destroy(direct);
		}
	}
	return 0;
}
