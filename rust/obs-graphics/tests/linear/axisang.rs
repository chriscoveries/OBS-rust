//! Tier 1: safe-API tests mirroring `test/cmocka/test_axisang.c`, plus edge
//! cases.

use core::f32::consts::{FRAC_1_SQRT_2, PI};

use obs_c_oracle as _;
use obs_graphics::axisang::AxisAng;
use obs_graphics::quat::Quat;
use proptest as _;

fn near(a: f32, b: f32) -> bool {
    (a - b).abs() < 1e-6
}

#[test]
fn test_axisang_from_quat_z90() {
    let a = AxisAng::from_quat(Quat::new(0.0, 0.0, FRAC_1_SQRT_2, FRAC_1_SQRT_2));
    assert!(near(a.x, 0.0) && near(a.y, 0.0) && near(a.z, 1.0), "{a:?}");
    assert!(near(a.w, PI / 2.0), "{a:?}");
}

#[test]
fn test_axisang_from_quat_identity_is_zero() {
    assert_eq!(
        AxisAng::from_quat(Quat::new(0.0, 0.0, 0.0, 1.0)),
        AxisAng::default()
    );
}

#[test]
fn test_axisang_from_quat_x180() {
    let a = AxisAng::from_quat(Quat::new(1.0, 0.0, 0.0, 0.0));
    assert!(near(a.x, 1.0) && near(a.y, 0.0) && near(a.z, 0.0), "{a:?}");
    assert!(near(a.w, PI), "{a:?}");
}

/// The axis is normalized even when the quaternion is not.
#[test]
fn axis_is_normalized() {
    let a = AxisAng::from_quat(Quat::new(0.0, 3.0, 4.0, 0.0));
    assert!(near(a.x, 0.0) && near(a.y, 0.6) && near(a.z, 0.8), "{a:?}");
}

/// A squared length of up to EPSILON counts as no rotation; above it does
/// not.
#[test]
fn epsilon_cutoff() {
    assert_eq!(
        AxisAng::from_quat(Quat::new(0.009, 0.0, 0.0, 1.0)),
        AxisAng::default()
    );
    let a = AxisAng::from_quat(Quat::new(0.011, 0.0, 0.0, 0.5));
    assert!(near(a.x, 1.0), "{a:?}");
}

/// NaN is not close to zero, so it takes the normalizing branch, as in C.
#[test]
fn nan_propagates() {
    let a = AxisAng::from_quat(Quat::new(f32::NAN, 0.0, 0.0, 1.0));
    assert!(a.x.is_nan() && a.y.is_nan() && a.z.is_nan());
}
