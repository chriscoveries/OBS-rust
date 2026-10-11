#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <cmocka.h>

#include "output-filename-format.h"

/* Upstream obsproject/obs-studio#13786 (reported by pricklythorn): with the
 * format "%CCYY-%MM-%DD %hh:%mm:%ss", Advanced mode recordings kept the ':'
 * in their names while saved replay buffers had it replaced. */
#define ISSUE_FORMAT "%CCYY-%MM-%DD %hh:%mm:%ss"

static void expect_format(const char *format, const char *prefix, const char *suffix, const char *expected)
{
	char *got = output_filename_format(format, prefix, suffix);

	if (!got) {
		fprintf(stderr, "output_filename_format(\"%s\") returned NULL\n", format ? format : "(null)");
		fail();
	}
	if (strcmp(got, expected) != 0) {
		fprintf(stderr,
			"output_filename_format(\"%s\", \"%s\", \"%s\")\n  got:      \"%s\"\n  expected: \"%s\"\n",
			format ? format : "(null)", prefix ? prefix : "(null)", suffix ? suffix : "(null)", got,
			expected);
		free(got);
		fail();
	}
	free(got);
}

/* The recording path passes no prefix or suffix. */
static void test_recording_format_replaces_colons(void **state)
{
	(void)state;
	expect_format(ISSUE_FORMAT, NULL, NULL, "%CCYY-%MM-%DD %hh_%mm_%ss");
	expect_format(ISSUE_FORMAT, "", "", "%CCYY-%MM-%DD %hh_%mm_%ss");
}

/* The replay buffer path adds its prefix and suffix to the same format. */
static void test_replay_buffer_format_replaces_colons(void **state)
{
	(void)state;
	expect_format(ISSUE_FORMAT, "Replay", NULL, "Replay %CCYY-%MM-%DD %hh_%mm_%ss");
	expect_format(ISSUE_FORMAT, "Replay ", " clip", "Replay %CCYY-%MM-%DD %hh_%mm_%ss clip");
	expect_format(ISSUE_FORMAT, NULL, "clip", "%CCYY-%MM-%DD %hh_%mm_%ss clip");
}

/* Recording and replay buffer agree on everything except the prefix. */
static void test_recording_matches_replay_buffer(void **state)
{
	(void)state;
	char *rec = output_filename_format(ISSUE_FORMAT, NULL, NULL);
	char *rb = output_filename_format(ISSUE_FORMAT, "Replay", NULL);

	if (!rec || !rb || strncmp(rb, "Replay ", 7) != 0 || strcmp(rec, rb + 7) != 0) {
		fprintf(stderr, "recording \"%s\" and replay buffer \"%s\" differ beyond the prefix\n",
			rec ? rec : "(null)", rb ? rb : "(null)");
		free(rec);
		free(rb);
		fail();
	}
	free(rec);
	free(rb);
}

static void test_every_reserved_character_is_replaced(void **state)
{
	(void)state;
	expect_format("a*b?c\"d|e:f>g<h", NULL, NULL, "a_b_c_d_e_f_g_h");
}

/* Separators in the format are subdirectories; they are kept and '\\' is
 * normalised to '/'. The prefix goes on the last component. */
static void test_subdirectories_in_format_are_kept(void **state)
{
	(void)state;
	expect_format("clips/%hh:%mm", NULL, NULL, "clips/%hh_%mm");
	expect_format("clips\\%hh:%mm", NULL, NULL, "clips/%hh_%mm");
	expect_format("clips/%hh:%mm", "Replay", NULL, "clips/Replay %hh_%mm");
	expect_format("a\\b/%hh", "Replay", NULL, "a/b/Replay %hh");
}

static void test_plain_formats_are_unchanged(void **state)
{
	(void)state;
	expect_format("%CCYY-%MM-%DD %hh-%mm-%ss", NULL, NULL, "%CCYY-%MM-%DD %hh-%mm-%ss");
	expect_format("caf\xC3\xA9 %FPS", NULL, NULL, "caf\xC3\xA9 %FPS");
	expect_format("", NULL, NULL, "");
	expect_format(NULL, NULL, NULL, "");
}

int main(void)
{
	const struct CMUnitTest tests[] = {
		cmocka_unit_test(test_recording_format_replaces_colons),
		cmocka_unit_test(test_replay_buffer_format_replaces_colons),
		cmocka_unit_test(test_recording_matches_replay_buffer),
		cmocka_unit_test(test_every_reserved_character_is_replaced),
		cmocka_unit_test(test_subdirectories_in_format_are_kept),
		cmocka_unit_test(test_plain_formats_are_unchanged),
	};

	return cmocka_run_group_tests(tests, NULL, NULL);
}
