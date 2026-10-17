//! Tier 1: safe-API tests mirroring `test/cmocka/test_audio_math.c`, plus
//! edge cases.
//!
//! As in the C test, only results every libm gets exactly are compared
//! exactly; the rest use the C test's relative tolerance. Tier 3 compares
//! every result bit for bit against the C on the same libm.

use obs_c_oracle as _;
use obs_media_io::audio_math::{db_to_mul, mul_to_db};
use proptest as _;

/// The C test's `assert_close`: about 8 float ULPs.
fn assert_close(actual: f32, expected: f32) {
    assert!(
        (actual - expected).abs() <= 1e-6 * expected.abs(),
        "{actual} is not close to {expected}"
    );
}

#[test]
fn test_mul_to_db_exact() {
    // 0 and -0 both compare equal to 0, so both give -inf.
    assert_eq!(mul_to_db(0.0), f32::NEG_INFINITY);
    assert_eq!(mul_to_db(-0.0), f32::NEG_INFINITY);

    assert_eq!(mul_to_db(1.0), 0.0);
    assert_eq!(mul_to_db(f32::INFINITY), f32::INFINITY);
}

#[test]
fn test_mul_to_db_nan() {
    // A negative multiplier has no dB value. Characterized, not endorsed.
    assert!(mul_to_db(-1.0).is_nan());
    assert!(mul_to_db(-f32::MIN_POSITIVE).is_nan());
    assert!(mul_to_db(f32::NEG_INFINITY).is_nan());
    assert!(mul_to_db(f32::NAN).is_nan());
}

#[test]
fn test_mul_to_db_values() {
    assert_close(mul_to_db(10.0), 20.0);
    assert_close(mul_to_db(0.1), -20.0);
    assert_close(mul_to_db(0.5), -6.020_6);
    assert_close(mul_to_db(2.0), 6.020_6);
    assert_close(mul_to_db(0.001), -60.0);

    // The float extremes, subnormal included, give finite dB.
    assert_close(mul_to_db(f32::MIN_POSITIVE), -758.595_6);
    assert_close(mul_to_db(f32::from_bits(1)), -897.069_4);
    assert_close(mul_to_db(f32::MAX), 770.636_8);
}

#[test]
fn test_db_to_mul_exact() {
    assert_eq!(db_to_mul(0.0), 1.0);
    assert_eq!(db_to_mul(-0.0), 1.0);

    // Non-finite input gives 0, including +inf. Characterized, not
    // endorsed.
    assert_eq!(db_to_mul(f32::INFINITY), 0.0);
    assert_eq!(db_to_mul(f32::NEG_INFINITY), 0.0);
    assert_eq!(db_to_mul(f32::NAN), 0.0);
}

#[test]
fn test_db_to_mul_range() {
    // Finite input past float range overflows to inf or underflows to 0.
    assert_eq!(db_to_mul(1000.0), f32::INFINITY);
    assert_eq!(db_to_mul(f32::MAX), f32::INFINITY);
    assert_eq!(db_to_mul(-1000.0), 0.0);
    assert_eq!(db_to_mul(-f32::MAX), 0.0);
}

#[test]
fn test_db_to_mul_values() {
    assert_close(db_to_mul(20.0), 10.0);
    assert_close(db_to_mul(-20.0), 0.1);
    assert_close(db_to_mul(-6.0), 0.501_187_2);
    assert_close(db_to_mul(6.0), 1.995_262_3);
    assert_close(db_to_mul(-60.0), 0.001);
}

#[test]
fn test_round_trip() {
    assert_close(db_to_mul(mul_to_db(0.25)), 0.25);
    assert_close(mul_to_db(db_to_mul(-12.0)), -12.0);
    assert_eq!(db_to_mul(mul_to_db(0.0)), 0.0);
}

/// The exact zeros are +0: `20 * +0` and the `0.0f` literal.
#[test]
fn zero_results_are_positive() {
    assert!(mul_to_db(1.0).is_sign_positive());
    assert!(db_to_mul(f32::NAN).is_sign_positive());
    assert!(db_to_mul(f32::NEG_INFINITY).is_sign_positive());
    assert!(db_to_mul(-1000.0).is_sign_positive());
}

/// NaN of either sign, or with any payload, is non-finite.
#[test]
fn db_to_mul_any_nan_is_zero() {
    for bits in [
        0x7fc0_0000,
        0xffc0_0000,
        0x7f80_0001,
        0xff80_0001,
        0x7fff_ffff,
    ] {
        assert_eq!(db_to_mul(f32::from_bits(bits)), 0.0, "{bits:#x}");
    }
}

/// The largest finite dB values that stay in range, for both signs.
#[test]
fn db_to_mul_near_float_range() {
    assert!(db_to_mul(770.0).is_finite());
    assert!(db_to_mul(-890.0) > 0.0);
}
