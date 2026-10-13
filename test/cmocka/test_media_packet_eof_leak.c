/* Verify that EOF returns the demux packet to media's packet pool. */

#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <setjmp.h>
#include <stdio.h>
#include <errno.h>

#include <cmocka.h>

#include <util/c99defs.h>
#include <util/darray.h>
#include <media-playback/media-playback.h>
#include <libavcodec/avcodec.h>
#include <libavcodec/packet.h>
#include <libavformat/avformat.h>

#define MAX_PACKET_READS 4096 /* Stop the test if the fixture unexpectedly never reaches EOF. */

static size_t packet_allocations;
static size_t packet_frees;
static bool force_read_result;
static int forced_read_result;
static AVPacket *tracked_packet_alloc(void);
static void tracked_packet_free(AVPacket **packet);
static int tracked_read_frame(AVFormatContext *format, AVPacket *packet);

static AVPacket *(*const real_packet_alloc)(void) = av_packet_alloc;
static void (*const real_packet_free)(AVPacket **) = av_packet_free;
static int (*const real_read_frame)(AVFormatContext *, AVPacket *) = av_read_frame;

#define av_packet_alloc tracked_packet_alloc
#define av_packet_free tracked_packet_free
#define av_read_frame tracked_read_frame
#include "media.c"
#undef av_packet_alloc
#undef av_packet_free
#undef av_read_frame

static const char *fixture_path;

static AVPacket *tracked_packet_alloc(void)
{
	AVPacket *packet = real_packet_alloc();
	if (packet)
		packet_allocations++;
	return packet;
}

static void tracked_packet_free(AVPacket **packet)
{
	if (packet && *packet)
		packet_frees++;
	real_packet_free(packet);
}

static int tracked_read_frame(AVFormatContext *format, AVPacket *packet)
{
	if (force_read_result) {
		force_read_result = false;
		return forced_read_result;
	}
	return real_read_frame(format, packet);
}

static void eof_releases_packet(void **state)
{
	UNUSED_PARAMETER(state);
	packet_allocations = 0;
	packet_frees = 0;

	mp_media_t media = {0};
	if (avformat_open_input(&media.fmt, fixture_path, NULL, NULL) < 0) {
		fprintf(stderr, "could not open cached media fixture\n");
		fail();
	}
	if (avformat_find_stream_info(media.fmt, NULL) < 0) {
		avformat_close_input(&media.fmt);
		fprintf(stderr, "could not read cached media fixture streams\n");
		fail();
	}
	da_init(media.packet_pool);

	/* No decoder owns packets in this focused test; successful demux reads use
	 * the same production pool return path as packets without a selected track. */
	unsigned reads = 0;
	int ret = 0;
	while (reads < MAX_PACKET_READS) {
		ret = mp_media_next_packet(&media);
		if (ret < 0)
			break;
		reads++;
	}

	for (size_t i = 0; i < media.packet_pool.num; i++)
		tracked_packet_free(&media.packet_pool.array[i]);
	da_free(media.packet_pool);
	avformat_close_input(&media.fmt);

	if (ret != AVERROR_EOF || packet_allocations != packet_frees)
		fprintf(stderr, "packet EOF: reads=%u ret=%d allocated=%zu freed=%zu\n", reads, ret, packet_allocations,
			packet_frees);
	assert_int_equal(ret, AVERROR_EOF);
	assert_true(reads > 0);
	assert_int_equal(packet_allocations, packet_frees);
}

static void read_error_releases_packet(void **state)
{
	UNUSED_PARAMETER(state);
	packet_allocations = 0;
	packet_frees = 0;
	force_read_result = true;
	forced_read_result = AVERROR(EIO);

	mp_media_t media = {0};
	da_init(media.packet_pool);
	int ret = mp_media_next_packet(&media);
	size_t pooled_packets = media.packet_pool.num;
	for (size_t i = 0; i < media.packet_pool.num; i++)
		tracked_packet_free(&media.packet_pool.array[i]);
	da_free(media.packet_pool);

	if (ret != AVERROR(EIO) || pooled_packets != 1 || packet_allocations != packet_frees)
		fprintf(stderr, "packet read error: ret=%d pooled=%zu allocated=%zu freed=%zu\n", ret, pooled_packets,
			packet_allocations, packet_frees);
	assert_int_equal(ret, AVERROR(EIO));
	assert_int_equal(pooled_packets, 1);
	assert_int_equal(packet_allocations, packet_frees);
}

int main(int argc, char **argv)
{
	if (argc != 2)
		return 2;
	fixture_path = argv[1];
	const struct CMUnitTest tests[] = {cmocka_unit_test(eof_releases_packet), cmocka_unit_test(read_error_releases_packet)};
	return cmocka_run_group_tests(tests, NULL, NULL);
}
