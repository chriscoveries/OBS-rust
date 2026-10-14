#include "../../libobs/obs-video-stats.h"

#include <stdlib.h>

#define CHECK(condition)         \
	do {                     \
		if (!(condition)) \
			abort();      \
	} while (0)

static struct obs_video_stats stats;

static void *publish_stats(void *unused)
{
	(void)unused;
	for (uint32_t i = 0; i < 100000; i++) {
		obs_video_stats_add_frames(&stats, 1, 1);
		obs_video_stats_set_timing(&stats, 60.0, 16666667);
	}
	return NULL;
}

static void *read_stats(void *unused)
{
	(void)unused;
	for (uint32_t i = 0; i < 100000; i++) {
		uint32_t total;
		uint32_t lagged;
		obs_video_stats_frame_counts(&stats, &total, &lagged);
		CHECK(total == lagged);
		CHECK(obs_video_stats_total_frames(&stats) <= 100000);
		CHECK(obs_video_stats_lagged_frames(&stats) <= 100000);
		CHECK(obs_video_stats_fps(&stats) <= 60.0);
		CHECK(obs_video_stats_avg_frame_time_ns(&stats) <= 16666667);
	}
	return NULL;
}

int main(void)
{
	CHECK(obs_video_stats_init(&stats) == 0);
	pthread_t writer;
	pthread_t reader;
	CHECK(pthread_create(&writer, NULL, publish_stats, NULL) == 0);
	CHECK(pthread_create(&reader, NULL, read_stats, NULL) == 0);
	CHECK(pthread_join(writer, NULL) == 0);
	CHECK(pthread_join(reader, NULL) == 0);
	obs_video_stats_destroy(&stats);
}
