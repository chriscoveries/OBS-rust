//! Tier 2: the `repr(C)` mirrors of the audio-io structs match the size,
//! alignment and field offsets the C compiler gives the originals in
//! `libobs/media-io/audio-io.h`.

use core::mem::{align_of, offset_of, size_of};

use obs_c_oracle as _;
use obs_c_oracle::audio_io_inlines as c;
use obs_media_io::ffi::audio_io as rs;
use obs_util as _;
use proptest as _;

#[test]
fn audio_data_layout_matches_c() {
    unsafe {
        assert_eq!(size_of::<rs::audio_data>(), c::oracle_audio_data_size());
        assert_eq!(align_of::<rs::audio_data>(), c::oracle_audio_data_align());
        assert_eq!(
            offset_of!(rs::audio_data, data),
            c::oracle_audio_data_offset_data()
        );
        assert_eq!(
            offset_of!(rs::audio_data, frames),
            c::oracle_audio_data_offset_frames()
        );
        assert_eq!(
            offset_of!(rs::audio_data, timestamp),
            c::oracle_audio_data_offset_timestamp()
        );
    }
}

#[test]
fn audio_output_data_layout_matches_c() {
    unsafe {
        assert_eq!(
            size_of::<rs::audio_output_data>(),
            c::oracle_audio_output_data_size()
        );
        assert_eq!(
            align_of::<rs::audio_output_data>(),
            c::oracle_audio_output_data_align()
        );
        assert_eq!(
            offset_of!(rs::audio_output_data, data),
            c::oracle_audio_output_data_offset_data()
        );
    }
}

#[test]
fn audio_output_info_layout_matches_c() {
    unsafe {
        assert_eq!(
            size_of::<rs::audio_output_info>(),
            c::oracle_audio_output_info_size()
        );
        assert_eq!(
            align_of::<rs::audio_output_info>(),
            c::oracle_audio_output_info_align()
        );
        assert_eq!(
            offset_of!(rs::audio_output_info, name),
            c::oracle_audio_output_info_offset_name()
        );
        assert_eq!(
            offset_of!(rs::audio_output_info, samples_per_sec),
            c::oracle_audio_output_info_offset_samples_per_sec()
        );
        assert_eq!(
            offset_of!(rs::audio_output_info, format),
            c::oracle_audio_output_info_offset_format()
        );
        assert_eq!(
            offset_of!(rs::audio_output_info, speakers),
            c::oracle_audio_output_info_offset_speakers()
        );
        assert_eq!(
            offset_of!(rs::audio_output_info, input_callback),
            c::oracle_audio_output_info_offset_input_callback()
        );
        assert_eq!(
            offset_of!(rs::audio_output_info, input_param),
            c::oracle_audio_output_info_offset_input_param()
        );
    }
}

#[test]
fn audio_convert_info_layout_matches_c() {
    unsafe {
        assert_eq!(
            size_of::<rs::audio_convert_info>(),
            c::oracle_audio_convert_info_size()
        );
        assert_eq!(
            align_of::<rs::audio_convert_info>(),
            c::oracle_audio_convert_info_align()
        );
        assert_eq!(
            offset_of!(rs::audio_convert_info, samples_per_sec),
            c::oracle_audio_convert_info_offset_samples_per_sec()
        );
        assert_eq!(
            offset_of!(rs::audio_convert_info, format),
            c::oracle_audio_convert_info_offset_format()
        );
        assert_eq!(
            offset_of!(rs::audio_convert_info, speakers),
            c::oracle_audio_convert_info_offset_speakers()
        );
        assert_eq!(
            offset_of!(rs::audio_convert_info, allow_clipping),
            c::oracle_audio_convert_info_offset_allow_clipping()
        );
    }
}
