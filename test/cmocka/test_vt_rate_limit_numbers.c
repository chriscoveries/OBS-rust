/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <stdbool.h>
#include <stdio.h>
#include <VideoToolbox/VideoToolbox.h>
#include <obs-module.h>

static unsigned checks;
static unsigned failures;
static unsigned limits_calls;
static double expected_bytes;
static double expected_seconds;

static void check(bool condition, const char *message)
{
	++checks;
	if (!condition) {
		++failures;
		fprintf(stderr, "%s\n", message);
	}
}

static OSStatus capture_rate_property(VTSessionRef session, CFStringRef key, CFTypeRef value)
{
	(void)session;
	if (CFEqual(key, kVTCompressionPropertyKey_DataRateLimits)) {
		++limits_calls;
		CFArrayRef limits = value;
		check(CFArrayGetCount(limits) == 2, "rate_limit_pair: expected byte and time entries");
		double bytes = -1.0;
		double seconds = -1.0;
		const bool bytes_ok =
			CFNumberGetValue(CFArrayGetValueAtIndex(limits, 0), kCFNumberDoubleType, &bytes);
		const bool seconds_ok =
			CFNumberGetValue(CFArrayGetValueAtIndex(limits, 1), kCFNumberDoubleType, &seconds);
		check(bytes_ok && bytes == expected_bytes,
		      "rate_limit_bytes_match_settings: encoded byte limit differs from requested limit");
		check(seconds_ok && seconds == expected_seconds,
		      "rate_limit_window_matches_settings: encoded window differs from requested seconds");
	}
	return noErr;
}

#define VTSessionSetProperty capture_rate_property
#include "../../plugins/mac-videotoolbox/encoder.c"
#undef VTSessionSetProperty

int main(void)
{
	const int rates[] = {50, 6000, 12000, 10000000};
	const double windows[] = {0.1, 0.35, 1.5, 10.0};
	for (size_t i = 0; i < sizeof(rates) / sizeof(rates[0]); ++i) {
		for (size_t j = 0; j < sizeof(windows) / sizeof(windows[0]); ++j) {
			expected_bytes = (double)rates[i] * 125.0 * windows[j];
			expected_seconds = windows[j];
			limits_calls = 0;
			const OSStatus status =
				session_set_bitrate(NULL, "ABR", 6000, 0.6f, true, rates[i], windows[j]);
			check(status == noErr, "rate_limit_status: unexpected error");
			check(limits_calls == 1, "rate_limit_calls: expected one limit submission");
		}
	}
	limits_calls = 0;
	session_set_bitrate(NULL, "ABR", 6000, 0.6f, false, 6000, 1.5);
	check(limits_calls == 0, "disabled_limit_does_not_submit: unexpected limit submission");
	printf("videotoolbox rate limits: checks=%u failures=%u\n", checks, failures);
	return failures ? 1 : 0;
}
