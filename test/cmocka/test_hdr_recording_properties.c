/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Native OBS property/data API test. The translated keys are sufficient for
 * this headless contract; the Qt settings window remains a Mac acceptance test. */
#include <obs.h>
#include <stdio.h>
#include <stdlib.h>
#include <limits.h>
#define obs_module_text(key) (key)
#include "../../plugins/mac-videotoolbox/hdr-recording-properties.h"
#define CHECK(x) do { if (!(x)) { fprintf(stderr, "line %d: %s\n", __LINE__, #x); exit(1); } } while (0)
int main(void)
{
	obs_data_t *settings = obs_data_create();
	hdr_recording_defaults(settings);
	struct hdr_recording_profile profile;
	CHECK(hdr_recording_read(settings, &profile) && !profile.enabled);
	obs_properties_t *props = obs_properties_create();
	vt_hdr_recording_properties(props);
	obs_property_t *group = obs_properties_get(props, HDR_RECORDING_ENABLE);
	CHECK(group && obs_property_group_type(group) == OBS_GROUP_CHECKABLE);
	CHECK(obs_properties_get(props, HDR_RECORDING_EXPOSURE));
	CHECK(obs_properties_get(props, HDR_RECORDING_WHITE));
	CHECK(obs_properties_get(props, HDR_RECORDING_PEAK));
	CHECK(obs_properties_get(props, HDR_RECORDING_KNEE));
	CHECK(obs_properties_get(props, HDR_RECORDING_ROLLOFF));
	vt_hdr_recording_modified(props, group, settings);
	CHECK(!obs_property_enabled(obs_properties_get(props, HDR_RECORDING_EXPOSURE)));
	obs_data_set_bool(settings, HDR_RECORDING_ENABLE, true);
	vt_hdr_recording_modified(props, group, settings);
	CHECK(hdr_recording_read(settings, &profile) && profile.enabled);
	CHECK(profile.white_nits == 203.0f && profile.peak_nits == 1000.0f);
	CHECK(obs_property_enabled(obs_properties_get(props, HDR_RECORDING_KNEE)));
	obs_data_set_int(settings, HDR_RECORDING_ROLLOFF, HDR_RECORDING_HARD);
	vt_hdr_recording_modified(props, group, settings);
	CHECK(!obs_property_enabled(obs_properties_get(props, HDR_RECORDING_KNEE)));
	CHECK(hdr_recording_read(settings, &profile) && profile.rolloff == HDR_RECORDING_HARD);
	obs_data_set_int(settings, HDR_RECORDING_ROLLOFF, LLONG_MAX);
	CHECK(!hdr_recording_read(settings, &profile));
	obs_data_set_bool(settings, HDR_RECORDING_ENABLE, false);
	CHECK(hdr_recording_read(settings, &profile) && !profile.enabled);
	obs_properties_destroy(props);
	obs_data_release(settings);
	puts("PASS: HDR property group, defaults, enable state, roll-off and invalid persisted settings");
	return 0;
}
