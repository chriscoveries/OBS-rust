#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <stdio.h>
#include <ctype.h>
#include <string.h>
#include <time.h>
#include <cmocka.h>

#include <obs-internal.h>
#include <caption/eia608.h>

#include "../../plugins/mac-captions/captions-filter.h"
#include "../../plugins/mac-captions/speech-transcriber.h"

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
	unsigned emitted_words;
};

struct fake_output {
	struct obs_output output;
	struct caption_track_data track;
};

static void *fake_create(void *factory_data)
{
	return factory_data;
}

static bool fake_start(void *session, uint32_t sample_rate, void (*word)(void *, const char *, double, double),
		       void *param)
{
	struct fake_transcriber *fake = session;
	fake->sample_rate_ok = sample_rate == TEST_SAMPLE_RATE;
	FILE *fixture = fopen(MAC_CAPTIONS_WORDS_FIXTURE, "r");
	if (!fixture)
		return false;
	while (fake->word_count < sizeof(fake->words) / sizeof(fake->words[0]) &&
	       fscanf(fixture, "%lf %31s", &fake->words[fake->word_count].at_sec, fake->words[fake->word_count].text) ==
		       2)
		++fake->word_count;
	fake->fixture_ok = !ferror(fixture) && fake->word_count == 3;
	fclose(fixture);
	bool ok = fake->sample_rate_ok && fake->fixture_ok;
	pthread_mutex_lock(&fake->mutex);
	if (ok) {
		fake->word = word;
		fake->word_param = param;
		fake->running = true;
		++fake->starts;
	}
	pthread_cond_broadcast(&fake->cond);
	pthread_mutex_unlock(&fake->mutex);
	return ok;
}

static bool fake_push(void *session, const float *mono, uint32_t frames, uint64_t timestamp_ns)
{
	struct fake_transcriber *fake = session;
	pthread_mutex_lock(&fake->mutex);
	if (!mono || !frames)
		fake->fixture_ok = false;
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

	for (size_t i = 0; word && i < due_count; ++i) {
		word(param, due[i].text, due[i].at_sec, due[i].at_sec + 0.12);
		pthread_mutex_lock(&fake->mutex);
		++fake->emitted_words;
		pthread_cond_broadcast(&fake->cond);
		pthread_mutex_unlock(&fake->mutex);
	}
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
	struct mac_captions_core *core = mac_captions_core_create(&fake_ops, &fake, NULL, TEST_SAMPLE_RATE, 1, false);
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
	assert_true(fake.fixture_ok);
	assert_true(fake.running);
	assert_true(mac_captions_core_is_running(core));
	wait_for_counter(&fake, &fake.emitted_words, 3);

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
	char hello[] = "hello";
	char from[] = "from";
	char fixture[] = "fixture";
	uint16_t hello_pair = eia608_from_utf8_2(&hello[0], &hello[1]);
	uint16_t from_pair = eia608_from_utf8_2(&from[0], &from[1]);
	uint16_t fixture_pair = eia608_from_utf8_2(&fixture[0], &fixture[1]);
	bool saw_hello = false;
	bool saw_from = false;
	bool saw_fixture = false;
	while (output.track.caption_data.size >= sizeof(triplet)) {
		deque_pop_front(&output.track.caption_data, triplet, sizeof(triplet));
		pair = (uint16_t)((triplet[1] << 8) | triplet[2]);
		saw_hello |= pair == hello_pair;
		saw_from |= pair == from_pair;
		saw_fixture |= pair == fixture_pair;
	}
	assert_true(saw_hello);
	assert_true(saw_from);
	assert_true(saw_fixture);

	mac_captions_core_set_enabled(core, false);
	wait_for_counter(&fake, &fake.stops, 1);
	assert_false(fake.running);
	assert_false(mac_captions_core_is_running(core));
	assert_false(mac_captions_core_drain_one(core, deliver_caption_to_output, &output.output));
	assert_int_equal(fake.starts, 1);

	deque_free(&output.track.caption_data);
	pthread_mutex_destroy(&output.track.caption_mutex);
	mac_captions_core_destroy(core);
	destroy_fake(&fake);
}

struct transcript_capture {
	char text[2048];
	size_t length;
};

static void capture_word(void *param, const char *text, double start_sec, double end_sec)
{
	(void)start_sec;
	(void)end_sec;
	struct transcript_capture *capture = param;
	if (capture->length && capture->length + 1 < sizeof(capture->text))
		capture->text[capture->length++] = ' ';
	for (const char *p = text; *p && capture->length + 1 < sizeof(capture->text); ++p)
		capture->text[capture->length++] = *p;
	capture->text[capture->length] = '\0';
}

static size_t normalized_words(const char *text, char words[][64], size_t capacity)
{
	size_t count = 0;
	while (*text && count < capacity) {
		while (*text && !isalnum((unsigned char)*text))
			++text;
		size_t length = 0;
		while (*text && isalnum((unsigned char)*text)) {
			if (length + 1 < sizeof(words[count]))
				words[count][length++] = (char)tolower((unsigned char)*text);
			++text;
		}
		if (length) {
			words[count][length] = '\0';
			++count;
		}
	}
	return count;
}

static void test_real_speech_file_transcript(void **state)
{
	(void)state;
	int32_t status = mac_captions_speech_asset_status();
	if (status != MAC_CAPTIONS_ASSETS_INSTALLED) {
		print_message("SKIP real speech: en_US SpeechTranscriber asset is not installed (status=%d)\n", status);
		skip();
	}

	struct transcript_capture capture = {0};
	assert_int_equal(mac_captions_speech_transcribe_file(MAC_CAPTIONS_SPEECH_FIXTURE, capture_word, &capture), 0);
	FILE *reference_file = fopen(MAC_CAPTIONS_REFERENCE_FIXTURE, "r");
	assert_non_null(reference_file);
	char reference[4096] = {0};
	assert_non_null(fgets(reference, sizeof(reference), reference_file));
	fclose(reference_file);

	char hypothesis_words[128][64] = {{0}};
	char reference_words[128][64] = {{0}};
	size_t hypothesis_count = normalized_words(capture.text, hypothesis_words, 128);
	size_t reference_count = normalized_words(reference, reference_words, 128);
	assert_true(reference_count > 0);
	assert_true(hypothesis_count > 0);

	unsigned distance[129][129] = {{0}};
	for (size_t i = 0; i <= hypothesis_count; ++i)
		distance[i][0] = (unsigned)i;
	for (size_t j = 0; j <= reference_count; ++j)
		distance[0][j] = (unsigned)j;
	for (size_t i = 1; i <= hypothesis_count; ++i) {
		for (size_t j = 1; j <= reference_count; ++j) {
			unsigned substitution =
				distance[i - 1][j - 1] + (strcmp(hypothesis_words[i - 1], reference_words[j - 1]) != 0);
			unsigned deletion = distance[i - 1][j] + 1;
			unsigned insertion = distance[i][j - 1] + 1;
			distance[i][j] = substitution < deletion ? substitution : deletion;
			if (insertion < distance[i][j])
				distance[i][j] = insertion;
		}
	}
	double wer = (double)distance[hypothesis_count][reference_count] / (double)reference_count;
	print_message("real speech transcript: %zu words; reference: %zu words; WER %.1f%%\n", hypothesis_count,
		      reference_count, wer * 100.0);
	print_message("hypothesis: %s\n", capture.text);
	assert_true(wer <= 0.50);
}

int main(void)
{
	const struct CMUnitTest tests[] = {
		cmocka_unit_test(test_file_audio_tap_enable_disable_rollup_and_output),
		cmocka_unit_test(test_real_speech_file_transcript),
	};
	return cmocka_run_group_tests(tests, NULL, NULL);
}
