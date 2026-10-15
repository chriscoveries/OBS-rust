/* SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once
#include "../../libobs/hdr-recording-settings.h"

static bool vt_hdr_recording_modified(obs_properties_t *props, obs_property_t *property, obs_data_t *settings)
{
	UNUSED_PARAMETER(property);
	const bool enabled = obs_data_get_bool(settings, HDR_RECORDING_ENABLE);
	const char *keys[] = {HDR_RECORDING_EXPOSURE, HDR_RECORDING_WHITE, HDR_RECORDING_PEAK,
			     HDR_RECORDING_ROLLOFF, HDR_RECORDING_KNEE};
	for (size_t i = 0; i < sizeof(keys) / sizeof(keys[0]); ++i) {
		obs_property_t *p = obs_properties_get(props, keys[i]);
		if (p)
			obs_property_set_enabled(p, enabled);
	}
	obs_property_t *knee = obs_properties_get(props, HDR_RECORDING_KNEE);
	if (knee)
		obs_property_set_enabled(knee, enabled && obs_data_get_int(settings, HDR_RECORDING_ROLLOFF) == HDR_RECORDING_SOFT);
	return true;
}

static void vt_hdr_recording_properties(obs_properties_t *props)
{
	obs_properties_t *group = obs_properties_create();
	obs_property_t *p = obs_properties_add_group(props, HDR_RECORDING_ENABLE, obs_module_text("HDRRecording"),
						   OBS_GROUP_CHECKABLE, group);
	obs_property_set_modified_callback(p, vt_hdr_recording_modified);
	p = obs_properties_add_text(group, "hdr_recording_help", obs_module_text("HDRRecording.Help"), OBS_TEXT_INFO);
	obs_property_text_set_info_word_wrap(p, true);
	p = obs_properties_add_float_slider(group, HDR_RECORDING_EXPOSURE, obs_module_text("HDRRecording.Exposure"), -4.0, 4.0, 0.1);
	obs_property_float_set_suffix(p, " EV");
	p = obs_properties_add_float_slider(group, HDR_RECORDING_WHITE, obs_module_text("HDRRecording.White"), 80.0, 400.0, 1.0);
	obs_property_float_set_suffix(p, " nits");
	p = obs_properties_add_float_slider(group, HDR_RECORDING_PEAK, obs_module_text("HDRRecording.Peak"), 400.0, 1000.0, 10.0);
	obs_property_float_set_suffix(p, " nits");
	p = obs_properties_add_list(group, HDR_RECORDING_ROLLOFF, obs_module_text("HDRRecording.Rolloff"),
				    OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_INT);
	obs_property_list_add_int(p, obs_module_text("HDRRecording.Soft"), HDR_RECORDING_SOFT);
	obs_property_list_add_int(p, obs_module_text("HDRRecording.Hard"), HDR_RECORDING_HARD);
	obs_property_set_modified_callback(p, vt_hdr_recording_modified);
	p = obs_properties_add_float_slider(group, HDR_RECORDING_KNEE, obs_module_text("HDRRecording.Knee"), 50.0, 95.0, 1.0);
	obs_property_float_set_suffix(p, " %");
}
