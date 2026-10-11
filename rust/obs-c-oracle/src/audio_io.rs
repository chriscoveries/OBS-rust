//! `libobs/media-io/audio-io.c` under `oracle_` renames: the C reference the
//! `obs-media-io` audio-io port is diffed against. Struct declarations are
//! independent of the port's, per policy.
use core::ffi::{c_char, c_int, c_void};

/// `audio_t`: opaque, same as in the C API.
pub enum OracleAudioOutput {}

/// Independent declaration of `struct audio_data`.
#[repr(C)]
#[allow(non_camel_case_types)]
#[derive(Debug, Clone, Copy)]
pub struct OracleAudioData {
    pub data: [*mut u8; 8], // MAX_AV_PLANES
    pub frames: u32,
    pub timestamp: u64,
}

/// Independent declaration of `struct audio_output_data`.
#[repr(C)]
#[allow(non_camel_case_types)]
#[derive(Debug, Clone, Copy)]
pub struct OracleAudioOutputData {
    pub data: [*mut f32; 8], // MAX_AUDIO_CHANNELS
}

/// Independent declaration of `struct audio_output_info`.
#[repr(C)]
#[allow(non_camel_case_types)]
#[derive(Debug, Clone, Copy)]
pub struct OracleAudioOutputInfo {
    pub name: *const c_char,
    pub samples_per_sec: u32,
    pub format: c_int,
    pub speakers: c_int,
    pub input_callback: Option<
        unsafe extern "C" fn(
            param: *mut c_void,
            start_ts: u64,
            end_ts: u64,
            new_ts: *mut u64,
            active_mixers: u32,
            mixes: *mut OracleAudioOutputData,
        ) -> bool,
    >,
    pub input_param: *mut c_void,
}

/// Independent declaration of `struct audio_convert_info`.
#[repr(C)]
#[allow(non_camel_case_types)]
#[derive(Debug, Clone, Copy)]
pub struct OracleAudioConvertInfo {
    pub samples_per_sec: u32,
    pub format: c_int,
    pub speakers: c_int,
    pub allow_clipping: bool,
}

pub type OracleAudioOutputCallback =
    Option<unsafe extern "C" fn(param: *mut c_void, mix_idx: usize, data: *mut OracleAudioData)>;

unsafe extern "C" {
    pub fn oracle_audio_output_open(
        audio: *mut *mut OracleAudioOutput,
        info: *mut OracleAudioOutputInfo,
    ) -> c_int;
    pub fn oracle_audio_output_close(audio: *mut OracleAudioOutput);
    pub fn oracle_audio_output_connect(
        audio: *mut OracleAudioOutput,
        mix_idx: usize,
        conversion: *const OracleAudioConvertInfo,
        callback: OracleAudioOutputCallback,
        param: *mut c_void,
    ) -> bool;
    pub fn oracle_audio_output_disconnect(
        audio: *mut OracleAudioOutput,
        mix_idx: usize,
        callback: OracleAudioOutputCallback,
        param: *mut c_void,
    );
    pub fn oracle_audio_output_active(audio: *const OracleAudioOutput) -> bool;
    pub fn oracle_audio_output_get_block_size(audio: *const OracleAudioOutput) -> usize;
    pub fn oracle_audio_output_get_planes(audio: *const OracleAudioOutput) -> usize;
    pub fn oracle_audio_output_get_channels(audio: *const OracleAudioOutput) -> usize;
    pub fn oracle_audio_output_get_sample_rate(audio: *const OracleAudioOutput) -> u32;
    pub fn oracle_audio_output_get_info(
        audio: *const OracleAudioOutput,
    ) -> *const OracleAudioOutputInfo;

    pub fn oracle_audio_tick(audio: *mut OracleAudioOutput, audio_time: u64, prev_time: u64);
    /// No-worker fixture: open/close an `audio_output` with no audio
    /// thread — the driver owns every tick via `oracle_audio_tick`.
    pub fn oracle_audio_output_open_no_thread(
        audio: *mut *mut OracleAudioOutput,
        info: *mut OracleAudioOutputInfo,
    ) -> c_int;
    pub fn oracle_audio_output_close_no_thread(audio: *mut OracleAudioOutput);
    pub fn oracle_audio_mix_input_count(audio: *mut OracleAudioOutput, mix_idx: usize) -> usize;
    pub fn oracle_audio_mix_input_resampled(
        audio: *mut OracleAudioOutput,
        mix_idx: usize,
        input_idx: usize,
    ) -> bool;
    pub fn oracle_audio_mix_input_conversion(
        audio: *mut OracleAudioOutput,
        mix_idx: usize,
        input_idx: usize,
    ) -> *const OracleAudioConvertInfo;

    pub fn oracle_audio_output_info_size() -> usize;
    pub fn oracle_audio_output_info_align() -> usize;
    pub fn oracle_audio_output_info_offset_name() -> usize;
    pub fn oracle_audio_output_info_offset_samples_per_sec() -> usize;
    pub fn oracle_audio_output_info_offset_format() -> usize;
    pub fn oracle_audio_output_info_offset_speakers() -> usize;
    pub fn oracle_audio_output_info_offset_input_callback() -> usize;
    pub fn oracle_audio_output_info_offset_input_param() -> usize;

    pub fn oracle_audio_convert_info_size() -> usize;
    pub fn oracle_audio_convert_info_align() -> usize;
    pub fn oracle_audio_convert_info_offset_samples_per_sec() -> usize;
    pub fn oracle_audio_convert_info_offset_format() -> usize;
    pub fn oracle_audio_convert_info_offset_speakers() -> usize;
    pub fn oracle_audio_convert_info_offset_allow_clipping() -> usize;

    pub fn oracle_audio_data_size() -> usize;
    pub fn oracle_audio_data_align() -> usize;
    pub fn oracle_audio_data_offset_data() -> usize;
    pub fn oracle_audio_data_offset_frames() -> usize;
    pub fn oracle_audio_data_offset_timestamp() -> usize;

    pub fn oracle_audio_output_data_size() -> usize;
    pub fn oracle_audio_output_data_align() -> usize;
    pub fn oracle_audio_output_data_offset_data() -> usize;
}
