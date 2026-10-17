//! Tier 1: safe-API tests mirroring `test/cmocka/test_vec2.c`, plus edge cases.

use obs_c_oracle as _;
use obs_graphics::vec2::Vec2;
use proptest as _;

#[test]
fn test_vec2_abs() {
    let dst = Vec2::new(-1.5, 2.0).abs();
    assert_eq!(dst.x, 1.5);
    assert_eq!(dst.y, 2.0);
}

#[test]
fn test_vec2_abs_negative_zero() {
    let dst = Vec2::new(-0.0, 0.0).abs();
    assert_eq!(dst.x, 0.0);
    assert_eq!(dst.y, 0.0);
    assert!(!dst.x.is_sign_negative());
    assert!(!dst.y.is_sign_negative());
}

#[test]
fn test_vec2_floor_ceil() {
    let v = Vec2::new(1.5, -1.5);

    let dst = v.floor();
    assert_eq!(dst.x, 1.0);
    assert_eq!(dst.y, -2.0);

    let dst = v.ceil();
    assert_eq!(dst.x, 2.0);
    assert_eq!(dst.y, -1.0);
}

#[test]
fn test_vec2_floor_ceil_integral() {
    let v = Vec2::new(3.0, -4.0);

    let dst = v.floor();
    assert_eq!(dst.x, 3.0);
    assert_eq!(dst.y, -4.0);

    let dst = v.ceil();
    assert_eq!(dst.x, 3.0);
    assert_eq!(dst.y, -4.0);
}

#[test]
fn test_vec2_close() {
    let a = Vec2::new(1.25, -3.5);
    let b = Vec2::new(1.25, -3.5);
    assert!(a.close(b, 0.0));
    assert!(a.close(b, 0.5));

    // a difference exactly equal to epsilon is close
    let a = Vec2::new(0.0, 0.0);
    let b = Vec2::new(0.5, 0.0);
    assert!(a.close(b, 0.5));

    // a larger difference in y only is not close
    let b = Vec2::new(0.0, 0.75);
    assert!(!a.close(b, 0.5));
}

#[test]
fn test_vec2_close_nan() {
    let a = Vec2::new(0.0, 0.0);

    let b = Vec2::new(f32::NAN, 0.0);
    assert!(!a.close(b, 1.0));
    assert!(!b.close(a, 1.0));

    let b = Vec2::new(0.0, f32::NAN);
    assert!(!a.close(b, 1.0));
    assert!(!b.close(a, 1.0));
}

#[test]
fn test_vec2_norm() {
    let dst = Vec2::new(3.0, 4.0).norm().expect("non-zero length");
    assert!((dst.x - 0.6).abs() <= 1e-6);
    assert!((dst.y - 0.8).abs() <= 1e-6);
}

#[test]
fn test_vec2_norm_zero_leaves_dst() {
    // None maps to the C behaviour of leaving dst unchanged.
    assert_eq!(Vec2::new(0.0, 0.0).norm(), None);
}

#[test]
fn test_vec2_norm_nan_leaves_dst() {
    assert_eq!(Vec2::new(f32::NAN, 0.0).norm(), None);
}

#[test]
fn infinity_components() {
    let v = Vec2::new(f32::NEG_INFINITY, f32::INFINITY);
    assert_eq!(v.abs(), Vec2::new(f32::INFINITY, f32::INFINITY));
    assert_eq!(v.floor(), v);
    assert_eq!(v.ceil(), v);
    // inf - inf is NaN, so an infinite vector is not close to itself.
    assert!(!v.close(v, f32::INFINITY));
    // len is inf, inv is 0, inf * 0 is NaN: C does the same.
    let n = v.norm().expect("infinite length is > 0");
    assert!(n.x.is_nan());
    assert!(n.y.is_nan());
}

#[test]
fn norm_of_tiny_subnormal_vector_is_none() {
    // x*x underflows to 0, so len is 0 and norm leaves dst unchanged.
    let tiny = f32::from_bits(1);
    assert!(tiny > 0.0 && !tiny.is_normal());
    assert_eq!(Vec2::new(tiny, tiny).len(), 0.0);
    assert_eq!(Vec2::new(tiny, tiny).norm(), None);
}
