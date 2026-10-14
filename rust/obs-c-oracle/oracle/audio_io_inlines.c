/* Test-only oracle: the static inline helpers in libobs/media-io/audio-io.h
 * are compiled into every caller, so this file exposes non-inline oracle_*
 * wrappers that call the real header functions, plus the layouts of
 * enum audio_format, enum speaker_layout, struct audio_data,
 * struct audio_output_data, struct audio_output_info and
 * struct audio_convert_info as the C compiler sees them, and the header's
 * size macros. The enums are passed as the ints they are. */
#include <stddef.h>

#include "media-io/audio-io.h"

/* Test-only exports called from Rust over FFI. */
uint32_t oracle_get_audio_channels(int speakers);
size_t oracle_get_audio_bytes_per_channel(int format);
bool oracle_is_audio_planar(int format);
size_t oracle_get_audio_planes(int format, int speakers);
size_t oracle_get_audio_size(int format, int speakers, uint32_t frames);
size_t oracle_get_total_audio_size(int format, int speakers, uint32_t frames);
uint64_t oracle_audio_frames_to_ns(size_t sample_rate, uint64_t frames);
uint64_t oracle_ns_to_audio_frames(size_t sample_rate, uint64_t frames);
size_t oracle_max_audio_mixes(void);
size_t oracle_max_audio_channels(void);
size_t oracle_max_device_input_channels(void);
size_t oracle_audio_output_frames(void);
size_t oracle_total_audio_size(void);
size_t oracle_audio_format_size(void);
size_t oracle_audio_format_align(void);
size_t oracle_audio_format_count(void);
int oracle_audio_format_value(size_t i);
size_t oracle_speaker_layout_size(void);
size_t oracle_speaker_layout_align(void);
size_t oracle_speaker_layout_count(void);
int oracle_speaker_layout_value(size_t i);
size_t oracle_audio_data_size(void);
size_t oracle_audio_data_align(void);
size_t oracle_audio_data_planes(void);
size_t oracle_audio_data_offset_data(void);
size_t oracle_audio_data_offset_frames(void);
size_t oracle_audio_data_offset_timestamp(void);
size_t oracle_audio_output_data_size(void);
size_t oracle_audio_output_data_align(void);
size_t oracle_audio_output_data_channels(void);
size_t oracle_audio_output_data_offset_data(void);
size_t oracle_audio_output_info_size(void);
size_t oracle_audio_output_info_align(void);
size_t oracle_audio_output_info_offset_name(void);
size_t oracle_audio_output_info_offset_samples_per_sec(void);
size_t oracle_audio_output_info_offset_format(void);
size_t oracle_audio_output_info_offset_speakers(void);
size_t oracle_audio_output_info_offset_input_callback(void);
size_t oracle_audio_output_info_offset_input_param(void);
size_t oracle_audio_input_callback_size(void);
size_t oracle_audio_convert_info_size(void);
size_t oracle_audio_convert_info_align(void);
size_t oracle_audio_convert_info_offset_samples_per_sec(void);
size_t oracle_audio_convert_info_offset_format(void);
size_t oracle_audio_convert_info_offset_speakers(void);
size_t oracle_audio_convert_info_offset_allow_clipping(void);

uint32_t oracle_get_audio_channels(int speakers)
{
	return get_audio_channels((enum speaker_layout)speakers);
}
size_t oracle_get_audio_bytes_per_channel(int format)
{
	return get_audio_bytes_per_channel((enum audio_format)format);
}
bool oracle_is_audio_planar(int format)
{
	return is_audio_planar((enum audio_format)format);
}
size_t oracle_get_audio_planes(int format, int speakers)
{
	return get_audio_planes((enum audio_format)format, (enum speaker_layout)speakers);
}
size_t oracle_get_audio_size(int format, int speakers, uint32_t frames)
{
	return get_audio_size((enum audio_format)format, (enum speaker_layout)speakers, frames);
}
size_t oracle_get_total_audio_size(int format, int speakers, uint32_t frames)
{
	return get_total_audio_size((enum audio_format)format, (enum speaker_layout)speakers, frames);
}
uint64_t oracle_audio_frames_to_ns(size_t sample_rate, uint64_t frames)
{
	return audio_frames_to_ns(sample_rate, frames);
}
uint64_t oracle_ns_to_audio_frames(size_t sample_rate, uint64_t frames)
{
	return ns_to_audio_frames(sample_rate, frames);
}

/* The size macros. */
size_t oracle_max_audio_mixes(void)
{
	return MAX_AUDIO_MIXES;
}
size_t oracle_max_audio_channels(void)
{
	return MAX_AUDIO_CHANNELS;
}
size_t oracle_max_device_input_channels(void)
{
	return MAX_DEVICE_INPUT_CHANNELS;
}
size_t oracle_audio_output_frames(void)
{
	return AUDIO_OUTPUT_FRAMES;
}
size_t oracle_total_audio_size(void)
{
	return TOTAL_AUDIO_SIZE;
}

/* Layout of enum audio_format. */
size_t oracle_audio_format_size(void)
{
	return sizeof(enum audio_format);
}
size_t oracle_audio_format_align(void)
{
	return _Alignof(enum audio_format);
}

/* Every enumerator, in header order. */
static const enum audio_format audio_formats[] = {
	AUDIO_FORMAT_UNKNOWN,      AUDIO_FORMAT_U8BIT,        AUDIO_FORMAT_16BIT,
	AUDIO_FORMAT_32BIT,        AUDIO_FORMAT_FLOAT,        AUDIO_FORMAT_U8BIT_PLANAR,
	AUDIO_FORMAT_16BIT_PLANAR, AUDIO_FORMAT_32BIT_PLANAR, AUDIO_FORMAT_FLOAT_PLANAR,
};

size_t oracle_audio_format_count(void)
{
	return sizeof(audio_formats) / sizeof(audio_formats[0]);
}
int oracle_audio_format_value(size_t i)
{
	return (int)audio_formats[i];
}

/* Layout of enum speaker_layout. */
size_t oracle_speaker_layout_size(void)
{
	return sizeof(enum speaker_layout);
}
size_t oracle_speaker_layout_align(void)
{
	return _Alignof(enum speaker_layout);
}

/* Every enumerator, in header order. */
static const enum speaker_layout speaker_layouts[] = {
	SPEAKERS_UNKNOWN, SPEAKERS_MONO,    SPEAKERS_STEREO,  SPEAKERS_2POINT1,
	SPEAKERS_4POINT0, SPEAKERS_4POINT1, SPEAKERS_5POINT1, SPEAKERS_7POINT1,
};

size_t oracle_speaker_layout_count(void)
{
	return sizeof(speaker_layouts) / sizeof(speaker_layouts[0]);
}
int oracle_speaker_layout_value(size_t i)
{
	return (int)speaker_layouts[i];
}

/* Layout of struct audio_data. */
size_t oracle_audio_data_size(void)
{
	return sizeof(struct audio_data);
}
size_t oracle_audio_data_align(void)
{
	return _Alignof(struct audio_data);
}
size_t oracle_audio_data_planes(void)
{
	return sizeof(((struct audio_data *)0)->data) / sizeof(((struct audio_data *)0)->data[0]);
}
size_t oracle_audio_data_offset_data(void)
{
	return offsetof(struct audio_data, data);
}
size_t oracle_audio_data_offset_frames(void)
{
	return offsetof(struct audio_data, frames);
}
size_t oracle_audio_data_offset_timestamp(void)
{
	return offsetof(struct audio_data, timestamp);
}

/* Layout of struct audio_output_data. */
size_t oracle_audio_output_data_size(void)
{
	return sizeof(struct audio_output_data);
}
size_t oracle_audio_output_data_align(void)
{
	return _Alignof(struct audio_output_data);
}
size_t oracle_audio_output_data_channels(void)
{
	return sizeof(((struct audio_output_data *)0)->data) / sizeof(((struct audio_output_data *)0)->data[0]);
}
size_t oracle_audio_output_data_offset_data(void)
{
	return offsetof(struct audio_output_data, data);
}

/* Layout of struct audio_output_info. */
size_t oracle_audio_output_info_size(void)
{
	return sizeof(struct audio_output_info);
}
size_t oracle_audio_output_info_align(void)
{
	return _Alignof(struct audio_output_info);
}
size_t oracle_audio_output_info_offset_name(void)
{
	return offsetof(struct audio_output_info, name);
}
size_t oracle_audio_output_info_offset_samples_per_sec(void)
{
	return offsetof(struct audio_output_info, samples_per_sec);
}
size_t oracle_audio_output_info_offset_format(void)
{
	return offsetof(struct audio_output_info, format);
}
size_t oracle_audio_output_info_offset_speakers(void)
{
	return offsetof(struct audio_output_info, speakers);
}
size_t oracle_audio_output_info_offset_input_callback(void)
{
	return offsetof(struct audio_output_info, input_callback);
}
size_t oracle_audio_output_info_offset_input_param(void)
{
	return offsetof(struct audio_output_info, input_param);
}
size_t oracle_audio_input_callback_size(void)
{
	return sizeof(audio_input_callback_t);
}

/* Layout of struct audio_convert_info. */
size_t oracle_audio_convert_info_size(void)
{
	return sizeof(struct audio_convert_info);
}
size_t oracle_audio_convert_info_align(void)
{
	return _Alignof(struct audio_convert_info);
}
size_t oracle_audio_convert_info_offset_samples_per_sec(void)
{
	return offsetof(struct audio_convert_info, samples_per_sec);
}
size_t oracle_audio_convert_info_offset_format(void)
{
	return offsetof(struct audio_convert_info, format);
}
size_t oracle_audio_convert_info_offset_speakers(void)
{
	return offsetof(struct audio_convert_info, speakers);
}
size_t oracle_audio_convert_info_offset_allow_clipping(void)
{
	return offsetof(struct audio_convert_info, allow_clipping);
}
