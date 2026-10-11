//! `struct media_frames_per_second` from `libobs/media-io/frame-rate.h` at
//! the C boundary. Its helpers are `static inline`, so nothing is exported
//! or swapped; the safe core is [`crate::frame_rate`].

use crate::frame_rate::MediaFramesPerSecond;

/// Mirrors `struct media_frames_per_second`.
#[repr(C)]
#[allow(non_camel_case_types)]
#[derive(Debug, Clone, Copy, Default, PartialEq, Eq)]
pub struct media_frames_per_second {
    pub numerator: u32,
    pub denominator: u32,
}

impl From<media_frames_per_second> for MediaFramesPerSecond {
    fn from(fps: media_frames_per_second) -> Self {
        Self {
            numerator: fps.numerator,
            denominator: fps.denominator,
        }
    }
}

impl From<MediaFramesPerSecond> for media_frames_per_second {
    fn from(fps: MediaFramesPerSecond) -> Self {
        Self {
            numerator: fps.numerator,
            denominator: fps.denominator,
        }
    }
}
