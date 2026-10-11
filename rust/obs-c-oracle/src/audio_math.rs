//! The `static inline` helpers in `media-io/audio-math.h`, as non-inline
//! wrappers (`oracle/audio_math.c`).

unsafe extern "C" {
    pub fn oracle_mul_to_db(mul: f32) -> f32;
    pub fn oracle_db_to_mul(db: f32) -> f32;
}
