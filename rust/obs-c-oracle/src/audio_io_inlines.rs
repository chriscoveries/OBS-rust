//! The `static inline` helpers in `media-io/audio-io.h`, as non-inline
//! wrappers (`oracle/audio_io_inlines.c`), and the layouts of the enums and
//! structs the header defines and its size macros. The enums are passed as
//! the `int`s they are.

use core::ffi::c_int;

unsafe extern "C" {
    pub fn oracle_get_audio_channels(speakers: c_int) -> u32;
    pub fn oracle_get_audio_bytes_per_channel(format: c_int) -> usize;
    pub fn oracle_is_audio_planar(format: c_int) -> bool;
    pub fn oracle_get_audio_planes(format: c_int, speakers: c_int) -> usize;
    pub fn oracle_get_audio_size(format: c_int, speakers: c_int, frames: u32) -> usize;
    pub fn oracle_get_total_audio_size(format: c_int, speakers: c_int, frames: u32) -> usize;
    pub fn oracle_audio_frames_to_ns(sample_rate: usize, frames: u64) -> u64;
    pub fn oracle_ns_to_audio_frames(sample_rate: usize, frames: u64) -> u64;

    pub fn oracle_max_audio_mixes() -> usize;
    pub fn oracle_max_audio_channels() -> usize;
    pub fn oracle_max_device_input_channels() -> usize;
    pub fn oracle_audio_output_frames() -> usize;
    pub fn oracle_total_audio_size() -> usize;

    pub fn oracle_audio_format_size() -> usize;
    pub fn oracle_audio_format_align() -> usize;
    pub fn oracle_audio_format_count() -> usize;
    pub fn oracle_audio_format_value(i: usize) -> c_int;

    pub fn oracle_speaker_layout_size() -> usize;
    pub fn oracle_speaker_layout_align() -> usize;
    pub fn oracle_speaker_layout_count() -> usize;
    pub fn oracle_speaker_layout_value(i: usize) -> c_int;

    pub fn oracle_audio_data_size() -> usize;
    pub fn oracle_audio_data_align() -> usize;
    pub fn oracle_audio_data_planes() -> usize;
    pub fn oracle_audio_data_offset_data() -> usize;
    pub fn oracle_audio_data_offset_frames() -> usize;
    pub fn oracle_audio_data_offset_timestamp() -> usize;

    pub fn oracle_audio_output_data_size() -> usize;
    pub fn oracle_audio_output_data_align() -> usize;
    pub fn oracle_audio_output_data_channels() -> usize;
    pub fn oracle_audio_output_data_offset_data() -> usize;

    pub fn oracle_audio_output_info_size() -> usize;
    pub fn oracle_audio_output_info_align() -> usize;
    pub fn oracle_audio_output_info_offset_name() -> usize;
    pub fn oracle_audio_output_info_offset_samples_per_sec() -> usize;
    pub fn oracle_audio_output_info_offset_format() -> usize;
    pub fn oracle_audio_output_info_offset_speakers() -> usize;
    pub fn oracle_audio_output_info_offset_input_callback() -> usize;
    pub fn oracle_audio_output_info_offset_input_param() -> usize;
    pub fn oracle_audio_input_callback_size() -> usize;

    pub fn oracle_audio_convert_info_size() -> usize;
    pub fn oracle_audio_convert_info_align() -> usize;
    pub fn oracle_audio_convert_info_offset_samples_per_sec() -> usize;
    pub fn oracle_audio_convert_info_offset_format() -> usize;
    pub fn oracle_audio_convert_info_offset_speakers() -> usize;
    pub fn oracle_audio_convert_info_offset_allow_clipping() -> usize;
}
