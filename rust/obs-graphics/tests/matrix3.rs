//! Tier 1: safe-API tests mirroring `test/cmocka/test_matrix3.c`.

use obs_c_oracle as _;
use obs_graphics::axisang::AxisAng;
use obs_graphics::matrix3::Matrix3;
use obs_graphics::matrix4::Matrix4;
use obs_graphics::plane::Plane;
use obs_graphics::quat::Quat;
use obs_graphics::vec3::Vec3;
use obs_graphics::vec4::Vec4;
use proptest as _;

fn row(x: f32, y: f32, z: f32, w: f32) -> Vec3 {
    Vec3::with_w(x, y, z, w)
}

fn identity() -> Matrix3 {
    Matrix3::new(
        row(1.0, 0.0, 0.0, 0.0),
        row(0.0, 1.0, 0.0, 0.0),
        row(0.0, 0.0, 1.0, 0.0),
        row(0.0, 0.0, 0.0, 0.0),
    )
}

fn assert_row(v: Vec3, x: f32, y: f32, z: f32, w: f32) {
    assert_eq!((v.x, v.y, v.z, v.w), (x, y, z, w));
}

fn assert_identity_axes(m: &Matrix3) {
    assert_row(m.x, 1.0, 0.0, 0.0, 0.0);
    assert_row(m.y, 0.0, 1.0, 0.0, 0.0);
    assert_row(m.z, 0.0, 0.0, 1.0, 0.0);
}

fn assert_identity(m: &Matrix3) {
    assert_identity_axes(m);
    assert_row(m.t, 0.0, 0.0, 0.0, 0.0);
}

/// A zero quaternion has norm 0, so the scale is 0 and the result is identity.
#[test]
fn test_matrix3_from_quat_zero_is_identity() {
    assert_identity(&Matrix3::from_quat(Quat::new(0.0, 0.0, 0.0, 0.0)));
}

#[test]
fn test_matrix3_from_quat_identity() {
    assert_identity(&Matrix3::from_quat(Quat::new(0.0, 0.0, 0.0, 1.0)));
    assert_identity(&Matrix3::from_quat(Quat::new(0.0, 0.0, 0.0, 2.0)));
}

/// 180 degrees about x: quat (1, 0, 0, 0).
#[test]
fn test_matrix3_from_quat_x180() {
    let m = Matrix3::from_quat(Quat::new(1.0, 0.0, 0.0, 0.0));
    assert_row(m.x, 1.0, 0.0, 0.0, 0.0);
    assert_row(m.y, 0.0, -1.0, 0.0, 0.0);
    assert_row(m.z, 0.0, 0.0, -1.0, 0.0);
    assert_row(m.t, 0.0, 0.0, 0.0, 0.0);
}

/// Angle 0 yields the identity quaternion, then the identity matrix.
#[test]
fn test_matrix3_from_axisang_zero() {
    assert_identity(&Matrix3::from_axisang(AxisAng::new(0.0, 0.0, 1.0, 0.0)));
}

/// Copies the four rows, then forces every w lane to 0.
#[test]
fn test_matrix3_from_matrix4_clears_w() {
    let src = Matrix4::new(
        Vec4::new(1.0, 2.0, 3.0, 4.0),
        Vec4::new(5.0, 6.0, 7.0, 8.0),
        Vec4::new(9.0, 10.0, 11.0, 12.0),
        Vec4::new(13.0, 14.0, 15.0, 16.0),
    );
    let m = Matrix3::from_matrix4(src);
    assert_row(m.x, 1.0, 2.0, 3.0, 0.0);
    assert_row(m.y, 5.0, 6.0, 7.0, 0.0);
    assert_row(m.z, 9.0, 10.0, 11.0, 0.0);
    assert_row(m.t, 13.0, 14.0, 15.0, 0.0);
}

#[test]
fn test_matrix3_mul_scale_and_alias() {
    let mut m1 = identity();
    m1.t = row(1.0, 2.0, 3.0, 0.0);
    let mut m2 = identity();
    m2.x.x = 2.0;
    m2.y.y = 3.0;
    m2.z.z = 4.0;
    let dst = m1 * &m2;
    assert_row(dst.x, 2.0, 0.0, 0.0, 0.0);
    assert_row(dst.y, 0.0, 3.0, 0.0, 0.0);
    assert_row(dst.z, 0.0, 0.0, 4.0, 0.0);
    assert_row(dst.t, 2.0, 6.0, 12.0, 0.0);
}

#[test]
fn test_matrix3_rotate_x180() {
    let mut m = identity();
    m.t = row(1.0, 2.0, 3.0, 0.0);
    let dst = m.rotate(Quat::new(1.0, 0.0, 0.0, 0.0));
    assert_row(dst.x, 1.0, 0.0, 0.0, 0.0);
    assert_row(dst.y, 0.0, -1.0, 0.0, 0.0);
    assert_row(dst.z, 0.0, 0.0, -1.0, 0.0);
    assert_row(dst.t, 1.0, -2.0, -3.0, 0.0);
}

#[test]
fn test_matrix3_rotate_aa_zero() {
    let mut m = identity();
    m.t = row(4.0, 5.0, 6.0, 9.0);
    let dst = m.rotate_aa(AxisAng::new(0.0, 1.0, 0.0, 0.0));
    assert_identity_axes(&dst);
    assert_row(dst.t, 4.0, 5.0, 6.0, 0.0);
}

/// `vec3_mul` multiplies every lane, including `w`.
#[test]
fn test_matrix3_scale_keeps_w() {
    let mut m = identity();
    m.x.w = 2.0;
    m.y.w = 4.0;
    m.z.w = 6.0;
    m.t.w = 8.0;
    let dst = m.scale(row(2.0, 3.0, 4.0, 5.0));
    assert_row(dst.x, 2.0, 0.0, 0.0, 10.0);
    assert_row(dst.y, 0.0, 3.0, 0.0, 20.0);
    assert_row(dst.z, 0.0, 0.0, 4.0, 30.0);
    assert_row(dst.t, 0.0, 0.0, 0.0, 40.0);
}

/// The SSE transpose writes `z.w` into the `w` lane of every axis row.
#[test]
fn test_matrix3_transpose_broadcasts_zw() {
    let mut m = identity();
    m.x.w = 3.0;
    m.y.w = 4.0;
    m.z.w = 7.0;
    m.t = row(1.0, 2.0, 3.0, 0.0);
    let dst = m.transpose();
    assert_row(dst.x, 1.0, 0.0, 0.0, 7.0);
    assert_row(dst.y, 0.0, 1.0, 0.0, 7.0);
    assert_row(dst.z, 0.0, 0.0, 1.0, 7.0);
    assert_row(dst.t, -1.0, -2.0, -3.0, 0.0);
}

#[test]
fn test_matrix3_inv_scale_and_translation() {
    let mut m = identity();
    m.x.x = 2.0;
    m.y.y = 4.0;
    m.z.z = 8.0;
    let dst = Matrix3::default().inv(m);
    assert_row(dst.x, 0.5, 0.0, 0.0, 0.0);
    assert_row(dst.y, 0.0, 0.25, 0.0, 0.0);
    assert_row(dst.z, 0.0, 0.0, 0.125, 0.0);
    assert_row(dst.t, 0.0, 0.0, 0.0, 0.0);

    let mut m = identity();
    m.t = row(4.0, -2.0, 8.0, 5.0);
    let dst = Matrix3::default().inv(m);
    assert_identity_axes(&dst);
    assert_row(dst.t, -4.0, 2.0, -8.0, 0.0);

    let mut m = identity();
    m.x.x = 0.125;
    m.y.y = 0.125;
    m.z.z = 0.125;
    let dst = Matrix3::default().inv(m);
    assert_row(dst.x, 8.0, 0.0, 0.0, 0.0);
    assert_row(dst.y, 0.0, 8.0, 0.0, 0.0);
    assert_row(dst.z, 0.0, 0.0, 8.0, 0.0);
    assert_row(dst.t, 0.0, 0.0, 0.0, 0.0);
}

/// Below the cutoff, the inverse writes nothing except `t.w = 0`.
#[test]
fn test_matrix3_inv_singular_keeps_dst() {
    let mut m = identity();
    m.x.x = 0.0625;
    m.y.y = 0.0625;
    m.z.z = 0.0625;
    let poisoned = Matrix3::new(
        row(9.0, 9.0, 9.0, 9.0),
        row(9.0, 9.0, 9.0, 9.0),
        row(9.0, 9.0, 9.0, 9.0),
        row(9.0, 9.0, 9.0, 9.0),
    );
    let dst = poisoned.inv(m);
    assert_row(dst.x, 9.0, 9.0, 9.0, 9.0);
    assert_row(dst.y, 9.0, 9.0, 9.0, 9.0);
    assert_row(dst.z, 9.0, 9.0, 9.0, 9.0);
    assert_row(dst.t, 9.0, 9.0, 9.0, 0.0);

    let m = Matrix3::new(
        row(0.0, 0.0, 0.0, 0.0),
        row(0.0, 0.0, 0.0, 0.0),
        row(0.0, 0.0, 0.0, 0.0),
        row(1.0, 2.0, 3.0, 5.0),
    );
    let dst = m.inv(m);
    assert_row(dst.x, 0.0, 0.0, 0.0, 0.0);
    assert_row(dst.y, 0.0, 0.0, 0.0, 0.0);
    assert_row(dst.z, 0.0, 0.0, 0.0, 0.0);
    assert_row(dst.t, 1.0, 2.0, 3.0, 0.0);
}

#[test]
fn test_matrix3_mirror_and_mirrorv() {
    let mut m = identity();
    m.t = row(1.0, 2.0, 3.0, 0.0);
    let dst = m.mirror(&Plane::new(row(1.0, 0.0, 0.0, 0.0), 0.0));
    assert_row(dst.x, -1.0, 0.0, 0.0, 0.0);
    assert_row(dst.y, 0.0, 1.0, 0.0, 0.0);
    assert_row(dst.z, 0.0, 0.0, 1.0, 0.0);
    assert_row(dst.t, -1.0, 2.0, 3.0, 0.0);

    let dst = m.mirrorv(row(0.0, 1.0, 0.0, 0.0));
    assert_row(dst.x, 1.0, 0.0, 0.0, 0.0);
    assert_row(dst.y, 0.0, -1.0, 0.0, 0.0);
    assert_row(dst.z, 0.0, 0.0, 1.0, 0.0);
    assert_row(dst.t, 1.0, -2.0, 3.0, 0.0);

    let mut m = identity();
    m.x.w = 2.0;
    let dst = m.mirrorv(row(1.0, 0.0, 0.0, 3.0));
    assert_row(dst.x, -13.0, 0.0, 0.0, 0.0);
    assert_row(dst.y, 0.0, 1.0, 0.0, 0.0);
    assert_row(dst.z, 0.0, 0.0, 1.0, 0.0);
    assert_row(dst.t, 0.0, 0.0, 0.0, 0.0);
}
