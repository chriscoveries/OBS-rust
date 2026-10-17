#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <stdio.h>
#include <cmocka.h>

#include <obs-module.h>
#include "oss-input-platform.h"

/*
 * The FreeBSD /dev/sndstat parser of the OSS input (obs-rust/obs-studio#864),
 * built on Linux from oss-input-freebsd.c: which devices become entries in
 * the device list, under what name and path.
 */

#define SNDSTAT "test_oss_sndstat.tmp"

static const char sample[] = "Installed devices:\n"
			     "pcm0: <Realtek ALC892 (Rear Analog)> (play/rec) default\n"
			     "pcm1: <Realtek ALC892 (Front Analog)> (play)\n"
			     "pcm2: <USB audio> (1p:1v/1r:1v)\n"
			     "pcm3: <Output only> (1p:1v/0r:0v)\n"
			     "\tsnddev flags=0x2e6<AUTOVCHAN,BUSY>\n"
			     "Installed devices from userspace:\n"
			     "dsp_virtual: <Virtual Device> (play/rec)\n"
			     "File Versions:\n"
			     "pcm9: <skipped> (play/rec)\n";

static void test_parses_capture_devices(void **state)
{
	UNUSED_PARAMETER(state);

	FILE *f = fopen(SNDSTAT, "w");
	assert_non_null(f);
	fputs(sample, f);
	fclose(f);

	obs_properties_t *props = obs_properties_create();
	obs_property_t *p =
		obs_properties_add_list(props, "dsp", "DSP", OBS_COMBO_TYPE_EDITABLE, OBS_COMBO_FORMAT_STRING);
	oss_sndstat_add_devices(p, SNDSTAT);
	remove(SNDSTAT);

	/* record-capable devices only: play/rec, or a mode with >= 1 record
	 * channel; the play-only pcm1, the record-less pcm3, the indented
	 * line and everything after "File Versions:" are skipped */
	assert_int_equal(obs_property_list_item_count(p), 3);
	assert_string_equal(obs_property_list_item_name(p, 0), "pcm0: Realtek ALC892 (Rear Analog)");
	assert_string_equal(obs_property_list_item_string(p, 0), "/dev/dsp0");
	assert_string_equal(obs_property_list_item_name(p, 1), "pcm2: USB audio");
	assert_string_equal(obs_property_list_item_string(p, 1), "/dev/dsp2");
	assert_string_equal(obs_property_list_item_name(p, 2), "dsp_virtual: Virtual Device");
	assert_string_equal(obs_property_list_item_string(p, 2), "/dev/dsp_virtual");

	obs_properties_destroy(props);
}

static void test_missing_file_adds_nothing(void **state)
{
	UNUSED_PARAMETER(state);

	obs_properties_t *props = obs_properties_create();
	obs_property_t *p =
		obs_properties_add_list(props, "dsp", "DSP", OBS_COMBO_TYPE_EDITABLE, OBS_COMBO_FORMAT_STRING);
	oss_sndstat_add_devices(p, "test_oss_sndstat.missing");
	assert_int_equal(obs_property_list_item_count(p), 0);
	obs_properties_destroy(props);
}

int main(void)
{
	const struct CMUnitTest tests[] = {
		cmocka_unit_test(test_parses_capture_devices),
		cmocka_unit_test(test_missing_file_adds_nothing),
	};

	return cmocka_run_group_tests(tests, NULL, NULL);
}
