/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Included after mp4_write_pasp, where mux/serializer helpers are defined. */
static bool mp4_hdr_recording_enabled(obs_encoder_t *enc)
{
	obs_data_t *settings = obs_encoder_get_settings(enc);
	const bool enabled = obs_data_get_bool(settings, HDR_RECORDING_ACTIVE);
	obs_data_release(settings);
	return enabled;
}

static size_t mp4_write_hdr_recording_ambient(struct mp4_mux *mux, obs_encoder_t *enc)
{
	if (!mp4_hdr_recording_enabled(enc))
		return 0;
	uint16_t pri = 0, trc = 0, spc = 0;
	uint8_t full = 0;
	get_colour_information(enc, &pri, &trc, &spc, &full);
	if (pri != 9 || trc != 18 || spc != 9 || full)
		return 0;
	/* Authored reference viewing environment: 314 lux, D65. This is not a
	 * measurement of the scene or the viewer's room. ISO amve is a Box,
	 * not a FullBox: 8-byte header, then an 8-byte payload. The units are
	 * 1/10000 lux and 1/50000 chromaticity (FFmpeg mov_read_amve). */
	struct serializer *s = mux->serializer;
	write_box(s, 16, "amve");
	s_wb32(s, 3140000);
	s_wb16(s, 15635);
	s_wb16(s, 16450);
	return 16;
}
