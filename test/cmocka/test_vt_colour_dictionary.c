/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <stdbool.h>
#include <stdio.h>
#include <VideoToolbox/VideoToolbox.h>

static unsigned checks;
static unsigned failures;
static CFIndex expected_count;
static OSStatus requested_status;
static CFStringRef expected_primaries;
static CFStringRef expected_transfer;
static CFStringRef expected_matrix;
static unsigned dictionary_calls;
static unsigned session_calls;

static void check(bool condition, const char *message)
{
	++checks;
	if (!condition) {
		++failures;
		fprintf(stderr, "%s\n", message);
	}
}

static CFDictionaryRef checked_dictionary(CFAllocatorRef allocator, const void **keys, const void **values,
					  CFIndex count, const CFDictionaryKeyCallBacks *key_callbacks,
					  const CFDictionaryValueCallBacks *value_callbacks)
{
	++dictionary_calls;
	check(count == expected_count, "colour_dictionary_count: wrong number of populated properties");
	CFIndex populated = 0;
	for (CFIndex i = 0; i < count; ++i) {
		check(keys[i] && values[i],
		      "sdr_properties_have_no_null_entries: NULL key or value passed to CFDictionaryCreate");
		if (!keys[i] || !values[i])
			break;
		++populated;
	}
	/* Report the invalid input as an assertion, then let the remaining checks run. */
	return CFDictionaryCreate(allocator, keys, values, populated, key_callbacks, value_callbacks);
}

static void check_value(CFDictionaryRef properties, CFStringRef key, CFStringRef expected, const char *message)
{
	CFTypeRef value = CFDictionaryGetValue(properties, key);
	check(value && CFEqual(value, expected), message);
}

static OSStatus capture_properties(VTSessionRef session, CFDictionaryRef properties)
{
	(void)session;
	++session_calls;
	check(CFDictionaryGetCount(properties) == expected_count,
	      "colour_dictionary_count: session received incomplete properties");
	check_value(properties, kVTCompressionPropertyKey_ColorPrimaries, expected_primaries,
		    "colour_dictionary_values: incorrect primaries");
	check_value(properties, kVTCompressionPropertyKey_TransferFunction, expected_transfer,
		    "colour_dictionary_values: incorrect transfer function");
	check_value(properties, kVTCompressionPropertyKey_YCbCrMatrix, expected_matrix,
		    "colour_dictionary_values: incorrect matrix");
	if (expected_count == 5) {
		CFDataRef mastering =
			CFDictionaryGetValue(properties, kVTCompressionPropertyKey_MasteringDisplayColorVolume);
		CFDataRef light = CFDictionaryGetValue(properties, kVTCompressionPropertyKey_ContentLightLevelInfo);
		check(mastering && CFDataGetLength(mastering) == 24,
		      "hdr_properties_keep_metadata: mastering data missing");
		check(light && CFDataGetLength(light) == 4, "hdr_properties_keep_metadata: light-level data missing");
	}
	return requested_status;
}

static float test_hdr_peak(void)
{
	return 1000.0f;
}

#include <obs-module.h>
#define CFDictionaryCreate checked_dictionary
#define VTSessionSetProperties capture_properties
#define obs_get_video_hdr_nominal_peak_level test_hdr_peak
#include "../../plugins/mac-videotoolbox/encoder.c"
#undef obs_get_video_hdr_nominal_peak_level
#undef VTSessionSetProperties
#undef CFDictionaryCreate

int main(void)
{
	const enum video_colorspace spaces[] = {VIDEO_CS_DEFAULT, VIDEO_CS_601,     VIDEO_CS_709,
						VIDEO_CS_SRGB,    VIDEO_CS_2100_PQ, VIDEO_CS_2100_HLG};
	for (size_t i = 0; i < sizeof(spaces) / sizeof(spaces[0]); ++i) {
		const enum video_colorspace cs = spaces[i];
		const bool hdr = cs == VIDEO_CS_2100_PQ || cs == VIDEO_CS_2100_HLG;
		expected_count = hdr ? 5 : 3;
		expected_primaries = hdr                  ? kCVImageBufferColorPrimaries_ITU_R_2020
				     : cs == VIDEO_CS_601 ? kCVImageBufferColorPrimaries_SMPTE_C
							  : kCVImageBufferColorPrimaries_ITU_R_709_2;
		expected_matrix = hdr                  ? kCVImageBufferYCbCrMatrix_ITU_R_2020
				  : cs == VIDEO_CS_601 ? kCVImageBufferYCbCrMatrix_ITU_R_601_4
						       : kCVImageBufferYCbCrMatrix_ITU_R_709_2;
		expected_transfer = cs == VIDEO_CS_2100_PQ    ? kCVImageBufferTransferFunction_SMPTE_ST_2084_PQ
				    : cs == VIDEO_CS_2100_HLG ? kCVImageBufferTransferFunction_ITU_R_2100_HLG
				    : cs == VIDEO_CS_SRGB     ? kCVImageBufferTransferFunction_sRGB
							      : kCVImageBufferTransferFunction_ITU_R_709_2;
		for (unsigned fail = 0; fail < 2; ++fail) {
			dictionary_calls = session_calls = 0;
			requested_status = fail ? kVTParameterErr : noErr;
			const OSStatus status = session_set_colorspace(NULL, cs);
			check(status == requested_status, "session_status_is_preserved: property error was replaced");
			check(dictionary_calls == 1 && session_calls == 1,
			      "colour_dictionary_calls: unexpected API count");
		}
	}
	printf("videotoolbox colour dictionary: checks=%u failures=%u\n", checks, failures);
	return failures ? 1 : 0;
}
