//! Tier 1: safe-API tests mirroring `test/cmocka/test_quat.c`, plus edge
//! cases.

use core::ops::Mul;

use obs_c_oracle as _;
use obs_graphics::axisang::AxisAng;
use obs_graphics::matrix3::Matrix3;
use obs_graphics::matrix4::Matrix4;
use obs_graphics::quat::Quat;
use obs_graphics::vec3::Vec3;
use obs_graphics::vec4::Vec4;

fn near(a: f32, b: f32) -> bool {
    (a - b).abs() < 1e-5
}

fn eq(q: Quat, x: f32, y: f32, z: f32, w: f32) -> bool {
    q.x == x && q.y == y && q.z == z && q.w == w
}

#[test]
fn test_quat_from_axisang_zero_is_identity() {
    let q = Quat::from_axisang(AxisAng::new(0.0, 1.0, 0.0, 0.0));
    assert!(eq(q, 0.0, 0.0, 0.0, 1.0), "{q:?}");
}

#[test]
fn test_quat_from_axisang_z90() {
    // Same literals as `test/cmocka/test_quat.c`, not the rounded constants.
    #[allow(clippy::approx_constant, clippy::excessive_precision)]
    let half = 0.70710678;
    #[allow(clippy::approx_constant, clippy::excessive_precision)]
    let angle = 3.14159265 / 2.0;
    let q = Quat::from_axisang(AxisAng::new(0.0, 0.0, 1.0, angle));
    assert!(near(q.x, 0.0) && near(q.y, 0.0), "{q:?}");
    assert!(near(q.z, half) && near(q.w, half), "{q:?}");
}

#[test]
fn test_quat_mul_identity() {
    let id = Quat::new(0.0, 0.0, 0.0, 1.0);
    assert!(eq(id.mul(id), 0.0, 0.0, 0.0, 1.0));
}

#[test]
fn test_quat_from_matrix4_identity() {
    let m = Matrix4::new(
        Vec4::new(1.0, 0.0, 0.0, 0.0),
        Vec4::new(0.0, 1.0, 0.0, 0.0),
        Vec4::new(0.0, 0.0, 1.0, 0.0),
        Vec4::new(0.0, 0.0, 0.0, 1.0),
    );
    assert!(eq(Quat::from_matrix4(&m), 0.0, 0.0, 0.0, 1.0));
}

#[test]
fn test_quat_from_matrix4_x180() {
    let m = Matrix4::new(
        Vec4::new(1.0, 0.0, 0.0, 0.0),
        Vec4::new(0.0, -1.0, 0.0, 0.0),
        Vec4::new(0.0, 0.0, -1.0, 0.0),
        Vec4::new(0.0, 0.0, 0.0, 1.0),
    );
    assert!(eq(Quat::from_matrix4(&m), 1.0, 0.0, 0.0, 0.0));
}

#[test]
fn test_quat_from_matrix3_identity() {
    let m = Matrix3::new(
        Vec3::new(1.0, 0.0, 0.0),
        Vec3::new(0.0, 1.0, 0.0),
        Vec3::new(0.0, 0.0, 1.0),
        Vec3::default(),
    );
    assert!(eq(Quat::from_matrix3(&m), 0.0, 0.0, 0.0, 1.0));
}

#[test]
fn test_quat_get_dir_identity_is_pos_z() {
    let d = Quat::new(0.0, 0.0, 0.0, 1.0).dir();
    assert_eq!([d.x, d.y, d.z, d.w], [0.0, 0.0, 1.0, 0.0]);
}

#[test]
fn test_quat_set_look_dir_neg_z_is_identity() {
    let q = Quat::from_look_dir(Vec3::new(0.0, 0.0, -1.0));
    assert!(eq(q, 0.0, 0.0, 0.0, 1.0), "{q:?}");
}

#[test]
fn test_quat_log_identity_is_zero() {
    let q = Quat::new(0.0, 0.0, 0.0, 1.0).log();
    assert!(eq(q, 0.0, 0.0, 0.0, 0.0), "{q:?}");
}

#[test]
fn test_quat_exp_zero_is_identity() {
    let q = Quat::new(0.0, 0.0, 0.0, 0.0).exp();
    assert!(eq(q, 0.0, 0.0, 0.0, 1.0), "{q:?}");
}

#[test]
fn test_quat_interpolate_identical_is_input() {
    let q = Quat::new(0.1, -0.2, 0.3, 0.4);
    assert!(eq(q.interpolate(q, 0.0), 0.1, -0.2, 0.3, 0.4));
    assert!(eq(q.interpolate(q, 1.0), 0.1, -0.2, 0.3, 0.4));
}

#[test]
fn test_quat_get_tangent_equal_inputs_is_zero() {
    let q = Quat::new(0.2, 0.3, -0.4, 0.5);
    assert!(eq(Quat::tangent(q, q, q), 0.0, 0.0, 0.0, 0.0));
}

#[test]
fn test_quat_interpolate_cubic_t0_is_q1() {
    let q1 = Quat::new(0.0, 0.0, 0.0, 1.0);
    let q2 = Quat::new(1.0, 0.0, 0.0, 0.0);
    let m1 = Quat::new(0.0, 1.0, 0.0, 0.0);
    let m2 = Quat::new(0.0, 0.0, 1.0, 0.0);
    assert!(eq(
        Quat::interpolate_cubic(q1, q2, m1, m2, 0.0),
        q1.x,
        q1.y,
        q1.z,
        q1.w
    ));
}
