//! The `static inline` helpers in `media-io/frame-rate.h`, as non-inline
//! wrappers (`oracle/frame_rate.c`), and the layout of
//! `struct media_frames_per_second`.

/// Independent declaration of `struct media_frames_per_second`.
/// Intentionally not shared with `obs-media-io`.
#[repr(C)]
#[derive(Debug, Clone, Copy, Default, PartialEq, Eq)]
pub struct OracleMediaFramesPerSecond {
    pub numerator: u32,
    pub denominator: u32,
}

unsafe extern "C" {
    pub fn oracle_media_frames_per_second_to_frame_interval(fps: OracleMediaFramesPerSecond)
    -> f64;
    pub fn oracle_media_frames_per_second_to_fps(fps: OracleMediaFramesPerSecond) -> f64;
    pub fn oracle_media_frames_per_second_is_valid(fps: OracleMediaFramesPerSecond) -> bool;

    pub fn oracle_media_frames_per_second_size() -> usize;
    pub fn oracle_media_frames_per_second_align() -> usize;
    pub fn oracle_media_frames_per_second_offset_numerator() -> usize;
    pub fn oracle_media_frames_per_second_offset_denominator() -> usize;
}
