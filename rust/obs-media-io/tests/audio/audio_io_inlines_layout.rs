//! Tier 2 (header-inline exemption): the `#[repr(C)]` mirrors of
//! `struct audio_data`, `struct audio_output_data`, `struct audio_output_info`
//! and `struct audio_convert_info` (`libobs/media-io/audio-io.h`) match the
//! layouts the C compiler produces, and so do `enum audio_format`,
//! `enum speaker_layout` and the header's size macros.

use core::ffi::c_int;
use core::mem::{align_of, offset_of, size_of};

use obs_c_oracle::audio_io_inlines as c;
use obs_media_io::audio_io::{
    AUDIO_OUTPUT_FRAMES, AudioFormat, MAX_AUDIO_CHANNELS, MAX_AUDIO_MIXES,
    MAX_DEVICE_INPUT_CHANNELS, SpeakerLayout, TOTAL_AUDIO_SIZE, audio_format_to_c,
    speaker_layout_to_c,
};
use obs_media_io::ffi::audio_io::{
    audio_convert_info, audio_data, audio_input_callback_t, audio_output_data, audio_output_info,
};
use obs_media_io::video_io::MAX_AV_PLANES;
use proptest as _;

#[test]
fn size_macros_match_c_header() {
    // SAFETY: the oracle layout functions take no arguments and only return constants.
    unsafe {
        assert_eq!(MAX_AUDIO_MIXES, c::oracle_max_audio_mixes());
        assert_eq!(MAX_AUDIO_CHANNELS, c::oracle_max_audio_channels());
        assert_eq!(
            MAX_DEVICE_INPUT_CHANNELS,
            c::oracle_max_device_input_channels()
        );
        assert_eq!(AUDIO_OUTPUT_FRAMES, c::oracle_audio_output_frames());
        assert_eq!(TOTAL_AUDIO_SIZE, c::oracle_total_audio_size());
    }
}

#[test]
fn audio_format_layout_matches_c_header() {
    // SAFETY: as above.
    unsafe {
        assert_eq!(size_of::<c_int>(), c::oracle_audio_format_size());
        assert_eq!(align_of::<c_int>(), c::oracle_audio_format_align());
    }
}

#[test]
fn audio_format_values_match_c_header() {
    // SAFETY: as above; `i` stays below the oracle's count.
    unsafe {
        assert_eq!(AudioFormat::ALL.len(), c::oracle_audio_format_count());
        for (i, format) in AudioFormat::ALL.into_iter().enumerate() {
            assert_eq!(
                audio_format_to_c(format),
                c::oracle_audio_format_value(i),
                "{format:?}"
            );
        }
    }
}

#[test]
fn speaker_layout_layout_matches_c_header() {
    // SAFETY: as above.
    unsafe {
        assert_eq!(size_of::<c_int>(), c::oracle_speaker_layout_size());
        assert_eq!(align_of::<c_int>(), c::oracle_speaker_layout_align());
    }
}

#[test]
fn speaker_layout_values_match_c_header() {
    // SAFETY: as above; `i` stays below the oracle's count.
    unsafe {
        assert_eq!(SpeakerLayout::ALL.len(), c::oracle_speaker_layout_count());
        for (i, speakers) in SpeakerLayout::ALL.into_iter().enumerate() {
            assert_eq!(
                speaker_layout_to_c(speakers),
                c::oracle_speaker_layout_value(i),
                "{speakers:?}"
            );
        }
    }
}

#[test]
fn audio_data_layout_matches_c_header() {
    // SAFETY: as above.
    unsafe {
        assert_eq!(MAX_AV_PLANES, c::oracle_audio_data_planes());
        assert_eq!(size_of::<audio_data>(), c::oracle_audio_data_size());
        assert_eq!(align_of::<audio_data>(), c::oracle_audio_data_align());
        assert_eq!(
            offset_of!(audio_data, data),
            c::oracle_audio_data_offset_data()
        );
        assert_eq!(
            offset_of!(audio_data, frames),
            c::oracle_audio_data_offset_frames()
        );
        assert_eq!(
            offset_of!(audio_data, timestamp),
            c::oracle_audio_data_offset_timestamp()
        );
    }
}

#[test]
fn audio_output_data_layout_matches_c_header() {
    // SAFETY: as above.
    unsafe {
        assert_eq!(MAX_AUDIO_CHANNELS, c::oracle_audio_output_data_channels());
        assert_eq!(
            size_of::<audio_output_data>(),
            c::oracle_audio_output_data_size()
        );
        assert_eq!(
            align_of::<audio_output_data>(),
            c::oracle_audio_output_data_align()
        );
        assert_eq!(
            offset_of!(audio_output_data, data),
            c::oracle_audio_output_data_offset_data()
        );
    }
}

#[test]
fn audio_output_info_layout_matches_c_header() {
    // SAFETY: as above.
    unsafe {
        assert_eq!(
            size_of::<audio_input_callback_t>(),
            c::oracle_audio_input_callback_size()
        );
        assert_eq!(
            size_of::<audio_output_info>(),
            c::oracle_audio_output_info_size()
        );
        assert_eq!(
            align_of::<audio_output_info>(),
            c::oracle_audio_output_info_align()
        );
        assert_eq!(
            offset_of!(audio_output_info, name),
            c::oracle_audio_output_info_offset_name()
        );
        assert_eq!(
            offset_of!(audio_output_info, samples_per_sec),
            c::oracle_audio_output_info_offset_samples_per_sec()
        );
        assert_eq!(
            offset_of!(audio_output_info, format),
            c::oracle_audio_output_info_offset_format()
        );
        assert_eq!(
            offset_of!(audio_output_info, speakers),
            c::oracle_audio_output_info_offset_speakers()
        );
        assert_eq!(
            offset_of!(audio_output_info, input_callback),
            c::oracle_audio_output_info_offset_input_callback()
        );
        assert_eq!(
            offset_of!(audio_output_info, input_param),
            c::oracle_audio_output_info_offset_input_param()
        );
    }
}

#[test]
fn audio_convert_info_layout_matches_c_header() {
    // SAFETY: as above.
    unsafe {
        assert_eq!(
            size_of::<audio_convert_info>(),
            c::oracle_audio_convert_info_size()
        );
        assert_eq!(
            align_of::<audio_convert_info>(),
            c::oracle_audio_convert_info_align()
        );
        assert_eq!(
            offset_of!(audio_convert_info, samples_per_sec),
            c::oracle_audio_convert_info_offset_samples_per_sec()
        );
        assert_eq!(
            offset_of!(audio_convert_info, format),
            c::oracle_audio_convert_info_offset_format()
        );
        assert_eq!(
            offset_of!(audio_convert_info, speakers),
            c::oracle_audio_convert_info_offset_speakers()
        );
        assert_eq!(
            offset_of!(audio_convert_info, allow_clipping),
            c::oracle_audio_convert_info_offset_allow_clipping()
        );
    }
}
