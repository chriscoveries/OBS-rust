/*
 * Regression test for cached seeks using microseconds against nanosecond
 * cache timestamps. The tiny Matroska fixture has distinct video luma and
 * audio sample markers for each 100 ms interval, so callbacks identify the
 * selected cached position.
 *
 * Fixture data/media_cached_seek_units.mkv (5 s: 16x16 ffv1 at 10 fps whose
 * luma is 5 * frame_index, plus 48 kHz mono s16 audio whose sample value is
 * 500 * (100 ms interval + 1)) was generated with:
 *
 *   ffmpeg -f lavfi -i "color=c=black:s=16x16:r=10:d=5,format=yuv420p,geq=lum='5*N':cb=128:cr=128" \
 *          -f lavfi -i "aevalsrc='500*(floor(t*10)+1)/32768':s=48000:d=5:c=mono" \
 *          -c:v ffv1 -c:a pcm_s16le -t 5 media_cached_seek_units.mkv
 *
 * (The same decoded content is committed under other names by sibling media
 * test branches; only the container bytes differ between ffmpeg runs.)
 */

#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <setjmp.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include <cmocka.h>

#include <util/c99defs.h>
#include <media-playback/media-playback.h>
#include <media-playback/cache.h>

/* Video frame 25 starts at exactly 2.5 s (luma 125, an exact-limit target for video).
 * The audio segment starting at exactly 2.496 s (117 * 1024 samples at 48 kHz) lies in the
 * 2.4 s interval (marker 12500), and the next segment, at 2.517 s, lies in the 2.5 s interval
 * (marker 13000), so a 2496 ms seek separates audio `>=` from `>`. */
#define VIDEO_BOUNDARY_SEEK_MS 2500
#define VIDEO_BOUNDARY_LUMA 125
#define VIDEO_BOUNDARY_AUDIO 13000
#define AUDIO_BOUNDARY_SEEK_MS 2496
#define AUDIO_BOUNDARY_LUMA 125
#define AUDIO_BOUNDARY_AUDIO 12500
#define WAIT_SECONDS 10

struct seek_watch {
	mp_cache_t *cache;
	pthread_mutex_t mutex;
	pthread_cond_t cond;
	unsigned video_callbacks;
	unsigned audio_callbacks;
	int64_t seek_ms;
	uint8_t video_luma[2];
	int16_t audio_sample[2];
};

static const char *fixture_path;
static mp_cache_t cache;
static struct seek_watch watch;
static bool cache_ready;
static bool watch_ready;

static void video_callback(void *opaque, struct obs_source_frame *frame)
{
	struct seek_watch *state = opaque;
	uint8_t luma = frame->data[0] ? frame->data[0][0] : 0;

	pthread_mutex_lock(&state->mutex);
	unsigned callback = state->video_callbacks++;
	if (callback < 2)
		state->video_luma[callback] = luma;
	pthread_cond_broadcast(&state->cond);
	pthread_mutex_unlock(&state->mutex);

	if (callback == 0)
		mp_cache_seek(state->cache, state->seek_ms);
}

static void audio_callback(void *opaque, struct obs_source_audio *audio)
{
	struct seek_watch *state = opaque;
	int16_t sample = 0;
	if (audio->data[0] && audio->frames)
		memcpy(&sample, audio->data[0], sizeof(sample));

	pthread_mutex_lock(&state->mutex);
	unsigned callback = state->audio_callbacks++;
	if (callback < 2)
		state->audio_sample[callback] = sample;
	pthread_cond_broadcast(&state->cond);
	pthread_mutex_unlock(&state->mutex);
}

static int start_cache(int64_t seek_ms)
{
	memset(&cache, 0, sizeof(cache));
	memset(&watch, 0, sizeof(watch));
	watch.cache = &cache;
	watch.seek_ms = seek_ms;
	if (pthread_mutex_init(&watch.mutex, NULL) != 0)
		return -1;
	if (pthread_cond_init(&watch.cond, NULL) != 0) {
		pthread_mutex_destroy(&watch.mutex);
		return -1;
	}
	watch_ready = true;

	struct mp_media_info info = {
		.opaque = &watch,
		.v_cb = video_callback,
		.a_cb = audio_callback,
		.path = fixture_path,
		.speed = 100,
		.is_local_file = true,
		.full_decode = true,
	};
	cache_ready = mp_cache_init(&cache, &info);
	return cache_ready ? 0 : -1;
}

static int video_boundary_setup(void **state)
{
	UNUSED_PARAMETER(state);
	return start_cache(VIDEO_BOUNDARY_SEEK_MS);
}

static int audio_boundary_setup(void **state)
{
	UNUSED_PARAMETER(state);
	return start_cache(AUDIO_BOUNDARY_SEEK_MS);
}

static int test_teardown(void **state)
{
	UNUSED_PARAMETER(state);
	if (cache_ready) {
		mp_cache_free(&cache);
		cache_ready = false;
	}
	if (watch_ready) {
		pthread_cond_destroy(&watch.cond);
		pthread_mutex_destroy(&watch.mutex);
		watch_ready = false;
	}
	return 0;
}

static void check_cached_seek(int expected_luma, int expected_audio)
{
	mp_cache_play(&cache, false);

	struct timespec deadline;
	assert_int_equal(timespec_get(&deadline, TIME_UTC), TIME_UTC);
	deadline.tv_sec += WAIT_SECONDS;

	pthread_mutex_lock(&watch.mutex);
	int wait_result = 0;
	while ((watch.video_callbacks < 2 || watch.audio_callbacks < 2) && wait_result == 0)
		wait_result = pthread_cond_timedwait(&watch.cond, &watch.mutex, &deadline);
	unsigned video_callbacks = watch.video_callbacks;
	unsigned audio_callbacks = watch.audio_callbacks;
	uint8_t selected_luma = watch.video_luma[1];
	int16_t selected_audio = watch.audio_sample[1];
	pthread_mutex_unlock(&watch.mutex);

	assert_int_equal(wait_result, 0);
	assert_true(video_callbacks >= 2);
	assert_true(audio_callbacks >= 2);
	/* Exact landing values, not lower bounds, so that an overshooting unit conversion
	 * (x10, x1000000), an undershooting one, and a strict '>' comparison are all caught. */
	if (selected_luma != expected_luma || selected_audio != expected_audio)
		fprintf(stderr, "cached seek selected luma=%u audio=%d; expected luma=%d audio=%d\n", selected_luma,
			selected_audio, expected_luma, expected_audio);
	assert_int_equal(selected_luma, expected_luma);
	assert_int_equal(selected_audio, expected_audio);
}

static void cached_seek_uses_nanosecond_timestamps(void **state)
{
	UNUSED_PARAMETER(state);
	check_cached_seek(VIDEO_BOUNDARY_LUMA, VIDEO_BOUNDARY_AUDIO);
}

static void cached_seek_audio_boundary_is_inclusive(void **state)
{
	UNUSED_PARAMETER(state);
	check_cached_seek(AUDIO_BOUNDARY_LUMA, AUDIO_BOUNDARY_AUDIO);
}

int main(int argc, char **argv)
{
	if (argc != 2)
		return 2;
	fixture_path = argv[1];
	const struct CMUnitTest tests[] = {
		cmocka_unit_test_setup_teardown(cached_seek_uses_nanosecond_timestamps, video_boundary_setup,
						test_teardown),
		cmocka_unit_test_setup_teardown(cached_seek_audio_boundary_is_inclusive, audio_boundary_setup,
						test_teardown),
	};
	return cmocka_run_group_tests(tests, NULL, NULL);
}
