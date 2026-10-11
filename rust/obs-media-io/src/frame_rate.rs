//! Port of the `static inline` helpers in `libobs/media-io/frame-rate.h`.

/// A frame rate as a fraction, the safe counterpart of
/// `struct media_frames_per_second`.
#[derive(Debug, Clone, Copy, Default, PartialEq, Eq, Hash)]
pub struct MediaFramesPerSecond {
    pub numerator: u32,
    pub denominator: u32,
}

impl MediaFramesPerSecond {
    /// `media_frames_per_second_to_frame_interval`: seconds per frame,
    /// `denominator / numerator` in `f64`. A zero part is divided as is.
    pub fn frame_interval(self) -> f64 {
        f64::from(self.denominator) / f64::from(self.numerator)
    }

    /// `media_frames_per_second_to_fps`: frames per second,
    /// `numerator / denominator` in `f64`. A zero part is divided as is.
    pub fn fps(self) -> f64 {
        f64::from(self.numerator) / f64::from(self.denominator)
    }

    /// `media_frames_per_second_is_valid`: both parts are non-zero.
    pub fn is_valid(self) -> bool {
        self.numerator != 0 && self.denominator != 0
    }
}
