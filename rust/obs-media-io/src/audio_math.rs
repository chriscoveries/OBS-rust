//! Port of the `static inline` helpers in `libobs/media-io/audio-math.h`.
//! Header-inline, so nothing is exported or swapped.
//!
//! Both call the platform libm (`log10f`, `powf`) like the header, so they
//! match the C bit for bit on each platform.

/// `mul_to_db`: a linear multiplier in decibels, `20 * log10(mul)`.
///
/// 0 (either sign) gives -inf. A negative multiplier or NaN gives NaN.
pub fn mul_to_db(mul: f32) -> f32 {
    if mul == 0.0 {
        f32::NEG_INFINITY
    } else {
        20.0 * mul.log10()
    }
}

/// `db_to_mul`: decibels as a linear multiplier, `10^(db / 20)`.
///
/// Non-finite input, +inf included, gives 0.
pub fn db_to_mul(db: f32) -> f32 {
    if db.is_finite() {
        10.0f32.powf(db / 20.0)
    } else {
        0.0
    }
}
