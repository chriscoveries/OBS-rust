#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <cmocka.h>

#include <obs-internal.h>
#include <caption/eia608.h>

#include "test_mac_captions_api.h"

#define TEST_SAMPLE_RATE 48000
#define TEST_FRAMES 480

struct fake_word_event {
	double at_sec;
	char text[32];
};

struct fake_transcriber {
	pthread_mutex_t mutex;
	pthread_cond_t cond;
	void (*word)(void *param, const char *text, double start_sec, double end_sec);
	void *word_param;
	uint64_t frames_seen;
	unsigned starts;
	unsigned stops;
	struct fake_word_event words[8];
	size_t word_count;
	size_t next_word;
	bool fixture_ok;
	bool sample_rate_ok;
	bool running;
};

struct fake_output {
	struct obs_output output;
	struct caption_track_data track;
};

static void *fake_create(void *factory_data)
{
	return factory_data;
}

static bool fake_start(void *session, uint32_t sample_rate,
		       void (*word)(void *, const char *, double, double), void *param)
{
	struct fake_transcriber *fake = session;
	fake->sample_rate_ok = sample_rate == TEST_SAMPLE_RATE;
	FILE *fixture = fopen(MAC_CAPTIONS_WORDS_FIXTURE, "r");
	if (!fixture)
		return false;
	while (fake->word_count < sizeof(fake->words) / sizeof(fake->words[0]) &&
	       fscanf(fixture, "%lf %31s", &fake->words[fake->word_count].at_sec,
		      fake->words[fake->word_count].text) == 2)
		++fake->word_count;
	fake->fixture_ok = !ferror(fixture) && fake->word_count == 3;
	fclose(fixture);
	pthread_mutex_lock(&fake->mutex);
	fake->word = word;
	fake->word_param = param;
	fake->running = true;
	++fake->starts;
	pthread_cond_broadcast(&fake->cond);
	pthread_mutex_unlock(&fake->mutex);
	return fake->sample_rate_ok && fake->fixture_ok;
}

static bool fake_push(void *session, const float *mono, uint32_t frames, uint64_t timestamp_ns)
{
	struct fake_transcriber *fake = session;
	assert_non_null(mono);
	assert_true(frames > 0);
	pthread_mutex_lock(&fake->mutex);
	fake->frames_seen += frames;
	double end = (double)timestamp_ns / 1e9 + (double)frames / TEST_SAMPLE_RATE;
	struct fake_word_event due[8];
	size_t due_count = 0;
	while (fake->next_word < fake->word_count && fake->words[fake->next_word].at_sec <= end) {
		due[due_count++] = fake->words[fake->next_word++];
	}
	void (*word)(void *, const char *, double, double) = fake->word;
	void *param = fake->word_param;
	pthread_cond_broadcast(&fake->cond);
	pthread_mutex_unlock(&fake->mutex);

	for (size_t i = 0; word && i < due_count; ++i)
		word(param, due[i].text, due[i].at_sec, due[i].at_sec + 0.12);
	return true;
}

static void fake_stop(void *session)
{
	struct fake_transcriber *fake = session;
	pthread_mutex_lock(&fake->mutex);
	fake->running = false;
	++fake->stops;
	pthread_cond_broadcast(&fake->cond);
	pthread_mutex_unlock(&fake->mutex);
}

static void fake_destroy(void *session)
{
	(void)session;
}

static const struct mac_captions_transcriber_ops fake_ops = {
	.create = fake_create,
	.start = fake_start,
	.push = fake_push,
	.stop = fake_stop,
	.destroy = fake_destroy,
};

static void wait_for_counter(struct fake_transcriber *fake, unsigned *value, unsigned target)
{
	struct timespec deadline;
	clock_gettime(CLOCK_REALTIME, &deadline);
	deadline.tv_sec += 3;
	pthread_mutex_lock(&fake->mutex);
	while (*value < target) {
		int rc = pthread_cond_timedwait(&fake->cond, &fake->mutex, &deadline);
		if (rc != 0)
			break;
	}
	assert_true(*value >= target);
	pthread_mutex_unlock(&fake->mutex);
}

static void wait_for_frames(struct fake_transcriber *fake, uint64_t target)
{
	struct timespec deadline;
	clock_gettime(CLOCK_REALTIME, &deadline);
	deadline.tv_sec += 3;
	pthread_mutex_lock(&fake->mutex);
	while (fake->frames_seen < target) {
		int rc = pthread_cond_timedwait(&fake->cond, &fake->mutex, &deadline);
		if (rc != 0)
			break;
	}
	assert_true(fake->frames_seen >= target);
	pthread_mutex_unlock(&fake->mutex);
}

static FILE *open_fixture(const char *path)
{
	FILE *file = fopen(path, "rb");
	assert_non_null(file);
	uint8_t header[44];
	assert_int_equal(fread(header, 1, sizeof(header), file), sizeof(header));
	assert_memory_equal(header, "RIFF", 4);
	assert_memory_equal(header + 8, "WAVE", 4);
	assert_memory_equal(header + 12, "fmt ", 4);
	assert_int_equal(header[22], 1);
	assert_int_equal(header[23], 0);
	assert_int_equal(header[34], 16);
	assert_int_equal(header[35], 0);
	assert_memory_equal(header + 36, "data", 4);
	return file;
}

static size_t feed_fixture(struct mac_captions_core *core, const char *path)
{
	FILE *file = open_fixture(path);
	int16_t pcm[TEST_FRAMES];
	float samples[TEST_FRAMES];
	size_t frames_total = 0;
	while (true) {
		size_t frames = fread(pcm, sizeof(*pcm), TEST_FRAMES, file);
		if (!frames)
			break;
		for (size_t i = 0; i < frames; ++i)
			samples[i] = (float)pcm[i] / 32768.0f;
		struct obs_audio_data audio = {0};
		audio.frames = (uint32_t)frames;
		audio.timestamp = (uint64_t)frames_total * 1000000000ULL / TEST_SAMPLE_RATE;
		audio.data[0] = (uint8_t *)samples;
		assert_ptr_equal(mac_captions_core_filter_audio(core, &audio), &audio);
		frames_total += frames;
	}
	assert_false(ferror(file));
	fclose(file);
	return frames_total;
}

static void deliver_caption_to_output(void *param, const uint8_t *data)
{
	struct obs_source_cea_708 captions = {.data = data, .packets = 1};
	obs_output_caption(param, &captions);
}

static void init_fake(struct fake_transcriber *fake)
{
	memset(fake, 0, sizeof(*fake));
	assert_int_equal(pthread_mutex_init(&fake->mutex, NULL), 0);
	assert_int_equal(pthread_cond_init(&fake->cond, NULL), 0);
}

static void destroy_fake(struct fake_transcriber *fake)
{
	pthread_cond_destroy(&fake->cond);
	pthread_mutex_destroy(&fake->mutex);
}

static void test_file_audio_tap_enable_disable_rollup_and_output(void **state)
{
	(void)state;
	struct fake_transcriber fake;
	init_fake(&fake);
	struct mac_captions_core *core = mac_captions_core_create(&fake_ops, &fake, TEST_SAMPLE_RATE, 1, false);
	assert_non_null(core);

	assert_true(feed_fixture(core, MAC_CAPTIONS_AUDIO_FIXTURE) >= TEST_SAMPLE_RATE);
	assert_int_equal(fake.starts, 0);
	assert_false(mac_captions_core_is_running(core));

	mac_captions_core_set_enabled(core, true);
	wait_for_counter(&fake, &fake.starts, 1);
	assert_true(fake.sample_rate_ok);
	assert_true(fake.fixture_ok);
	assert_true(feed_fixture(core, MAC_CAPTIONS_AUDIO_FIXTURE) >= TEST_SAMPLE_RATE);
	wait_for_frames(&fake, TEST_SAMPLE_RATE);
	assert_true(mac_captions_core_is_running(core));

	struct fake_output output = {0};
	pthread_mutex_init(&output.track.caption_mutex, NULL);
	deque_init(&output.track.caption_data);
	output.output.caption_tracks[0] = &output.track;
	unsigned ticks = 0;
	while (mac_captions_core_drain_one(core, deliver_caption_to_output, &output.output)) {
		assert_int_equal(output.track.caption_data.size, (size_t)(++ticks) * 3);
		assert_true(ticks < 128);
	}
	assert_true(ticks > 12);

	uint8_t triplet[3];
	deque_pop_front(&output.track.caption_data, triplet, sizeof(triplet));
	assert_int_equal(triplet[0], 0xFC);
	uint16_t pair = (uint16_t)((triplet[1] << 8) | triplet[2]);
	assert_int_equal(pair, eia608_control_command(eia608_control_erase_display_memory, 0));

	mac_captions_core_set_enabled(core, false);
	wait_for_counter(&fake, &fake.stops, 1);
	assert_false(mac_captions_core_is_running(core));
	assert_false(mac_captions_core_drain_one(core, deliver_caption_to_output, &output.output));
	assert_int_equal(fake.starts, 1);

	deque_free(&output.track.caption_data);
	pthread_mutex_destroy(&output.track.caption_mutex);
	mac_captions_core_destroy(core);
	destroy_fake(&fake);
}

int main(void)
{
	const struct CMUnitTest tests[] = {
		cmocka_unit_test(test_file_audio_tap_enable_disable_rollup_and_output),
	};
	return cmocka_run_group_tests(tests, NULL, NULL);
}
