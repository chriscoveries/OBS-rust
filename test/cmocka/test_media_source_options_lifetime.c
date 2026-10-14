#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <setjmp.h>
#include <stdatomic.h>
#include <pthread.h>
#include <unistd.h>
#include <cmocka.h>

#include <libavutil/dict.h>

#include <util/bmem.h>
#include <util/threading.h>

#include <media-playback/media-playback.h>

#define OPTIONS_TEXT "probesize=32"

/*
 * media_playback is compiled into this executable, so the linker can wrap its
 * calls to FFmpeg's option parser and to bfree without touching libobs. The
 * parser wrapper records which pointer media playback hands to FFmpeg, the
 * bfree wrapper counts frees of the pointers under watch, and the pthread_join
 * wrapper releases a held parser only once the worker is being joined. That
 * checks the ownership contract directly and does not need a sanitizer to see
 * it.
 */
static os_event_t *parse_entered;
static os_event_t *release_parse;
static atomic_bool block_next_parse;
static atomic_bool release_on_join;
static atomic_bool parse_in_progress;
static atomic_int parsed_frees_in_parse;
static atomic_int parse_result;
static atomic_int parse_count;
static atomic_uintptr_t parsed_ptr;
static atomic_uintptr_t caller_ptr;
static atomic_int parsed_frees;
static atomic_int caller_frees;
static char parsed_text[64];

int __real_av_dict_parse_string(AVDictionary **pm, const char *key_val, const char *key_val_sep, const char *pairs_sep,
				int flags);
void __real_bfree(void *ptr);
int __real_pthread_join(pthread_t thread, void **retval);

int __wrap_av_dict_parse_string(AVDictionary **pm, const char *key_val, const char *key_val_sep, const char *pairs_sep,
				int flags)
{
	/* Snapshot before any wait: this is the string the parser is given. */
	snprintf(parsed_text, sizeof(parsed_text), "%s", key_val);
	atomic_store_explicit(&parsed_ptr, (uintptr_t)key_val, memory_order_release);
	atomic_fetch_add_explicit(&parse_count, 1, memory_order_acq_rel);

	atomic_store_explicit(&parse_in_progress, true, memory_order_release);

	if (atomic_exchange_explicit(&block_next_parse, false, memory_order_acq_rel)) {
		os_event_signal(parse_entered);
		if (os_event_timedwait(release_parse, 5000) != 0) {
			atomic_store_explicit(&parse_in_progress, false, memory_order_release);
			return -1;
		}
	}

	int ret = __real_av_dict_parse_string(pm, key_val, key_val_sep, pairs_sep, flags);
	atomic_store_explicit(&parse_result, ret, memory_order_release);
	atomic_store_explicit(&parse_in_progress, false, memory_order_release);
	return ret;
}

/* Default visibility: with a sanitizer runtime in the link, the linker also
 * redirects the runtime's own pthread_join reference to this wrapper. */
__attribute__((visibility("default"))) int __wrap_pthread_join(pthread_t thread, void **retval)
{
	/* A held parser is only let go once media playback has asked its worker
	 * to stop and is joining it, so anything freed before this point was
	 * freed while the worker could still read it. */
	if (atomic_exchange_explicit(&release_on_join, false, memory_order_acq_rel))
		os_event_signal(release_parse);
	return __real_pthread_join(thread, retval);
}

void __wrap_bfree(void *ptr)
{
	if (ptr) {
		if ((uintptr_t)ptr == atomic_load_explicit(&parsed_ptr, memory_order_acquire)) {
			atomic_fetch_add_explicit(&parsed_frees, 1, memory_order_acq_rel);
			if (atomic_load_explicit(&parse_in_progress, memory_order_acquire))
				atomic_fetch_add_explicit(&parsed_frees_in_parse, 1, memory_order_acq_rel);
		}
		if ((uintptr_t)ptr == atomic_load_explicit(&caller_ptr, memory_order_acquire))
			atomic_fetch_add_explicit(&caller_frees, 1, memory_order_acq_rel);
	}
	__real_bfree(ptr);
}

static int reset_probe(void **state)
{
	UNUSED_PARAMETER(state);

	atomic_store(&block_next_parse, false);
	atomic_store(&release_on_join, false);
	atomic_store(&parse_in_progress, false);
	atomic_store(&parsed_frees_in_parse, 0);
	atomic_store(&parse_result, -1);
	atomic_store(&parse_count, 0);
	atomic_store(&parsed_ptr, 0);
	atomic_store(&caller_ptr, 0);
	atomic_store(&parsed_frees, 0);
	atomic_store(&caller_frees, 0);
	parsed_text[0] = '\0';
	return 0;
}

/* The parser must get media playback's own copy, never the caller's buffer,
 * and that copy must be released exactly once. */
static void assert_parser_got_owned_copy(const char *caller_options)
{
	assert_int_equal(atomic_load(&parse_count), 1);
	assert_string_equal(parsed_text, OPTIONS_TEXT);
	assert_true(atomic_load(&parsed_ptr) != 0);
	assert_true(atomic_load(&parsed_ptr) != (uintptr_t)caller_options);
	assert_int_equal(atomic_load(&parsed_frees), 1);
}

static void media_worker_keeps_options_until_parse_finishes(void **state)
{
	UNUSED_PARAMETER(state);

	assert_int_equal(os_event_init(&parse_entered, OS_EVENT_TYPE_MANUAL), 0);
	assert_int_equal(os_event_init(&release_parse, OS_EVENT_TYPE_MANUAL), 0);

	char *options = bstrdup(OPTIONS_TEXT);
	assert_non_null(options);
	struct mp_media_info info = {
		.path = "/dev/null",
		.ffmpeg_options = options,
		.speed = 100,
		.is_local_file = true,
	};

	atomic_store_explicit(&block_next_parse, true, memory_order_release);
	atomic_store_explicit(&release_on_join, true, memory_order_release);
	media_playback_t *media = media_playback_create(&info);
	assert_non_null(media);
	assert_int_equal(os_event_timedwait(parse_entered, 5000), 0);

	/* The worker is inside the parser with the string it was handed. If
	 * that is the caller's own buffer, the free below is the bug: stop the
	 * worker cleanly and fail instead of freeing under it. */
	if (atomic_load(&parsed_ptr) == (uintptr_t)options) {
		os_event_signal(release_parse);
		media_playback_destroy(media);
		fail_msg("media playback parsed the caller's options buffer");
	}

	/* Mirrors ffmpeg_source_update freeing the settings string before the
	 * old media object is destroyed and its worker is joined. The worker
	 * stays inside the parser until the join releases it. */
	bfree(options);
	media_playback_destroy(media);

	assert_int_equal(atomic_load_explicit(&parse_result, memory_order_acquire), 0);
	/* The copy must outlive the parse: freeing it before the join frees it
	 * under the worker. */
	assert_int_equal(atomic_load(&parsed_frees_in_parse), 0);
	assert_parser_got_owned_copy(options);
	os_event_destroy(parse_entered);
	os_event_destroy(release_parse);
}

/* Full-decode playback parses on the caller's thread and fails to open
 * /dev/null, so mp_cache_init takes its failure path. The copy made for the
 * parse must still be released there. */
static void cache_init_failure_releases_options_copy(void **state)
{
	UNUSED_PARAMETER(state);

	char *options = bstrdup(OPTIONS_TEXT);
	assert_non_null(options);
	atomic_store(&caller_ptr, (uintptr_t)options);
	struct mp_media_info info = {
		.path = "/dev/null",
		.ffmpeg_options = options,
		.speed = 100,
		.is_local_file = true,
		.full_decode = true,
	};

	media_playback_t *media = media_playback_create(&info);
	assert_null(media);

	assert_parser_got_owned_copy(options);
	assert_int_equal(atomic_load(&caller_frees), 0);
	bfree(options);
}

static bool write_silent_wav(char *path, uint8_t format_tag_lo, uint8_t format_tag_hi)
{
	uint8_t header[44] = {
		'R', 'I', 'F', 'F', 0x24, 0x06, 0,   0,   'W', 'A',  'V',  'E',  'f', 'm',  't',
		' ', 16,  0,   0,   0,    1,    0,   1,   0,   0x40, 0x1f, 0,    0,   0x80, 0x3e,
		0,   0,   2,   0,   16,   0,    'd', 'a', 't', 'a',  0x00, 0x06, 0,   0,
	};
	static const uint8_t samples[1536];

	/* Bytes 20-21 are the WAVE format tag. */
	header[20] = format_tag_lo;
	header[21] = format_tag_hi;

	int fd = mkstemp(path);
	if (fd < 0)
		return false;
	bool ok = write(fd, header, sizeof(header)) == (ssize_t)sizeof(header) &&
		  write(fd, samples, sizeof(samples)) == (ssize_t)sizeof(samples);
	close(fd);
	return ok;
}

/* On a successful full decode the media copy is released by the decode
 * itself, and neither it nor the cache may free the caller's string. */
static void cache_decode_releases_options_copy(void **state)
{
	UNUSED_PARAMETER(state);

	char path[] = "/tmp/obs_media_options_XXXXXX";
	assert_true(write_silent_wav(path, 0x01, 0x00));

	char *options = bstrdup(OPTIONS_TEXT);
	assert_non_null(options);
	atomic_store(&caller_ptr, (uintptr_t)options);
	struct mp_media_info info = {
		.path = path,
		.ffmpeg_options = options,
		.speed = 100,
		.is_local_file = true,
		.full_decode = true,
	};

	media_playback_t *media = media_playback_create(&info);
	assert_non_null(media);
	media_playback_destroy(media);
	unlink(path);

	assert_parser_got_owned_copy(options);
	assert_int_equal(atomic_load(&caller_frees), 0);
	bfree(options);
}

/* A WAV with an unknown format tag opens, then fails to find a decoder, so
 * mp_cache_init fails with the media already opened. The copy must be released
 * exactly once there: once by mp_media_free, not again by the cache. */
static void cache_open_then_init_failure_releases_options_copy(void **state)
{
	UNUSED_PARAMETER(state);

	char path[] = "/tmp/obs_media_options_XXXXXX";
	assert_true(write_silent_wav(path, 0x34, 0x12));

	char *options = bstrdup(OPTIONS_TEXT);
	assert_non_null(options);
	atomic_store(&caller_ptr, (uintptr_t)options);
	struct mp_media_info info = {
		.path = path,
		.ffmpeg_options = options,
		.speed = 100,
		.is_local_file = true,
		.full_decode = true,
	};

	media_playback_t *media = media_playback_create(&info);
	unlink(path);
	assert_null(media);

	assert_parser_got_owned_copy(options);
	assert_int_equal(atomic_load(&caller_frees), 0);
	bfree(options);
}

int main(void)
{
	const struct CMUnitTest tests[] = {
		cmocka_unit_test_setup(media_worker_keeps_options_until_parse_finishes, reset_probe),
		cmocka_unit_test_setup(cache_init_failure_releases_options_copy, reset_probe),
		cmocka_unit_test_setup(cache_decode_releases_options_copy, reset_probe),
		cmocka_unit_test_setup(cache_open_then_init_failure_releases_options_copy, reset_probe),
	};

	return cmocka_run_group_tests(tests, NULL, NULL);
}
