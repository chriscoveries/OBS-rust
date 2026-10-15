/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Included by obs-encoder.c in place of its GPU-rescale implementation.
 * Keep public encoder/video structs and headers unchanged. */
static bool hdr_recording_shader_ready(void)
{
	static const char *const names[] = {"hdr_recording_enabled", "hdr_recording_exposure", "hdr_recording_peak",
					  "hdr_recording_knee", "hdr_recording_soft"};
	bool ready = true;
	obs_enter_graphics();
	gs_effect_t *effect = obs->video.conversion_effect;
	for (size_t i = 0; i < sizeof(names) / sizeof(names[0]); ++i) {
		if (!effect || !gs_effect_get_param_by_name(effect, names[i])) {
			ready = false;
			break;
		}
	}
	obs_leave_graphics();
	return ready;
}

static bool maybe_set_up_gpu_rescale(struct obs_encoder *encoder)
{
	struct hdr_recording_profile profile;
	if (!hdr_recording_read(encoder->context.settings, &profile)) {
		obs_encoder_set_last_error(encoder, "Invalid HDR recording tone settings.");
		return false;
	}
	obs_data_set_bool(encoder->context.settings, HDR_RECORDING_ACTIVE, false);
	if (!encoder->media)
		return !profile.enabled;
	if (profile.enabled && (encoder->orig_info.type != OBS_ENCODER_VIDEO || !encoder->orig_info.codec ||
				strcmp(encoder->orig_info.codec, "hevc") != 0)) {
		obs_encoder_set_last_error(encoder, "HDR recording requires a separate HEVC recording encoder.");
		return false;
	}
	if (!profile.enabled && encoder->gpu_scale_type == OBS_SCALE_DISABLE)
		return true;
	if (!profile.enabled && !encoder->scaled_height && !encoder->scaled_width &&
	    encoder->preferred_format == VIDEO_FORMAT_NONE && encoder->preferred_space == VIDEO_CS_DEFAULT &&
	    encoder->preferred_range == VIDEO_RANGE_DEFAULT)
		return true;

	const struct video_output_info *info = video_output_get_info(encoder->media);
	struct obs_core_video_mix *current_mix = get_mix_for_video(encoder->media);
	if (!info || !current_mix) {
		if (profile.enabled)
			obs_encoder_set_last_error(encoder, "HDR recording requires an OBS-rendered video mix.");
		return !profile.enabled;
	}
	const uint32_t width = encoder->scaled_width ? encoder->scaled_width : info->width;
	const uint32_t height = encoder->scaled_height ? encoder->scaled_height : info->height;
	const enum obs_scale_type scale_type = encoder->gpu_scale_type == OBS_SCALE_DISABLE ? OBS_SCALE_BILINEAR
										    : encoder->gpu_scale_type;
	const enum video_format format = profile.enabled ? VIDEO_FORMAT_P010
					: encoder->preferred_format != VIDEO_FORMAT_NONE ? encoder->preferred_format
												: info->format;
	const enum video_colorspace space = profile.enabled ? VIDEO_CS_2100_HLG
					  : encoder->preferred_space != VIDEO_CS_DEFAULT ? encoder->preferred_space
												 : info->colorspace;
	const enum video_range_type range = profile.enabled ? VIDEO_RANGE_PARTIAL
					   : encoder->preferred_range != VIDEO_RANGE_DEFAULT ? encoder->preferred_range
												     : info->range;
	if (profile.enabled && (!width || !height || (width & 1u) || (height & 1u))) {
		obs_encoder_set_last_error(encoder, "HDR recording requires positive, even output dimensions.");
		return false;
	}
	if (profile.enabled && !hdr_recording_shader_ready()) {
		obs_encoder_set_last_error(encoder, "HDR recording shader is unavailable; check the application resources.");
		return false;
	}
	if (!current_mix->encoder_only_mix)
		encoder->original_video = encoder->media;

	struct obs_core_video_mix *created = NULL;
	/* Search before allocating, then search again before publishing. Both
	 * searches use the same full tone-profile key and acquire a reference. */
	for (int attempt = 0; attempt < 2; ++attempt) {
		pthread_mutex_lock(&obs->video.mixes_mutex);
		for (size_t i = 0; i < obs->video.mixes.num; ++i) {
			struct obs_core_video_mix *candidate = obs->video.mixes.array[i];
			const struct video_output_info *voi = video_output_get_info(candidate->video);
			if (candidate->view != current_mix->view || candidate->ovi.scale_type != scale_type ||
			    !hdr_recording_profile_equal(&candidate->hdr_recording, &profile) ||
			    voi->width != width || voi->height != height || voi->format != format ||
			    voi->colorspace != space || voi->range != range)
				continue;
			candidate->encoder_refs += 1;
			obs_encoder_set_video(encoder, candidate->video);
			obs_data_set_bool(encoder->context.settings, HDR_RECORDING_ACTIVE, profile.enabled);
			pthread_mutex_unlock(&obs->video.mixes_mutex);
			if (created)
				obs_free_video_mix(created);
			return true;
		}
		if (created) {
			da_push_back(obs->video.mixes, &created);
			obs_encoder_set_video(encoder, created->video);
			obs_data_set_bool(encoder->context.settings, HDR_RECORDING_ACTIVE, profile.enabled);
			pthread_mutex_unlock(&obs->video.mixes_mutex);
			return true;
		}
		pthread_mutex_unlock(&obs->video.mixes_mutex);
		struct obs_video_info ovi = current_mix->ovi;
		ovi.output_format = format;
		ovi.colorspace = space;
		ovi.range = range;
		ovi.output_width = width;
		ovi.output_height = height;
		ovi.scale_type = scale_type;
		ovi.gpu_conversion = true;
		created = obs_create_video_mix(&ovi);
		if (!created) {
			if (profile.enabled)
				obs_encoder_set_last_error(encoder, "Unable to create the 10-bit HLG recording mix.");
			return !profile.enabled;
		}
		created->encoder_only_mix = true;
		created->encoder_refs = 1;
		created->view = current_mix->view;
		created->hdr_recording = profile;
	}
	/* Every second iteration above either publishes or reuses a mix. */
	return false;
}
