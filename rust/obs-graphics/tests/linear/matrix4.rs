//! Tier 1: safe-API tests mirroring `test/cmocka/test_matrix4.c`.

use obs_c_oracle as _;
use obs_graphics::axisang::AxisAng;
use obs_graphics::matrix3::Matrix3;
use obs_graphics::matrix4::Matrix4;
use obs_graphics::quat::Quat;
use obs_graphics::vec3::Vec3;
use obs_graphics::vec4::Vec4;
use proptest as _;

fn row(x: f32, y: f32, z: f32, w: f32) -> Vec4 {
    Vec4::new(x, y, z, w)
}

fn identity() -> Matrix4 {
    Matrix4::new(
        row(1.0, 0.0, 0.0, 0.0),
        row(0.0, 1.0, 0.0, 0.0),
        row(0.0, 0.0, 1.0, 0.0),
        row(0.0, 0.0, 0.0, 1.0),
    )
}

fn assert_row(v: Vec4, x: f32, y: f32, z: f32, w: f32) {
    assert_eq!((v.x, v.y, v.z, v.w), (x, y, z, w));
}

fn assert_identity(m: &Matrix4) {
    assert_row(m.x, 1.0, 0.0, 0.0, 0.0);
    assert_row(m.y, 0.0, 1.0, 0.0, 0.0);
    assert_row(m.z, 0.0, 0.0, 1.0, 0.0);
    assert_row(m.t, 0.0, 0.0, 0.0, 1.0);
}

fn x180() -> Matrix4 {
    Matrix4::from_quat(Quat::new(1.0, 0.0, 0.0, 0.0))
}

fn translation(x: f32, y: f32, z: f32) -> Matrix4 {
    let mut m = identity();
    m.t = row(x, y, z, 1.0);
    m
}

fn fill(v: f32) -> Matrix4 {
    Matrix4::new(
        row(v, v, v, v),
        row(v, v, v, v),
        row(v, v, v, v),
        row(v, v, v, v),
    )
}

/// A zero quaternion has norm 0, so the scale is 0. Axes are identity with
/// w = 0, and the translation lane is 1.
#[test]
fn test_matrix4_from_quat_zero_is_identity() {
    assert_identity(&Matrix4::from_quat(Quat::new(0.0, 0.0, 0.0, 0.0)));
}

#[test]
fn test_matrix4_from_quat_identity() {
    assert_identity(&Matrix4::from_quat(Quat::new(0.0, 0.0, 0.0, 1.0)));
    assert_identity(&Matrix4::from_quat(Quat::new(0.0, 0.0, 0.0, 2.0)));
}

/// 180 degrees about x: quat (1, 0, 0, 0).
#[test]
fn test_matrix4_from_quat_x180() {
    let m = x180();
    assert_row(m.x, 1.0, 0.0, 0.0, 0.0);
    assert_row(m.y, 0.0, -1.0, 0.0, 0.0);
    assert_row(m.z, 0.0, 0.0, -1.0, 0.0);
    assert_row(m.t, 0.0, 0.0, 0.0, 1.0);
}

/// Angle 0 yields the identity quaternion, then the identity matrix.
#[test]
fn test_matrix4_from_axisang_zero() {
    assert_identity(&Matrix4::from_axisang(AxisAng::new(0.0, 0.0, 1.0, 0.0)));
}

/// Copies every lane, then forces t.w to 1. Axis w lanes survive.
#[test]
fn test_matrix4_from_matrix3_sets_tw() {
    let src = Matrix3::new(
        Vec3::with_w(1.0, 2.0, 3.0, 4.0),
        Vec3::with_w(5.0, 6.0, 7.0, 8.0),
        Vec3::with_w(9.0, 10.0, 11.0, 12.0),
        Vec3::with_w(13.0, 14.0, 15.0, 16.0),
    );
    let m = Matrix4::from_matrix3(src);
    assert_row(m.x, 1.0, 2.0, 3.0, 4.0);
    assert_row(m.y, 5.0, 6.0, 7.0, 8.0);
    assert_row(m.z, 9.0, 10.0, 11.0, 12.0);
    assert_row(m.t, 13.0, 14.0, 15.0, 1.0);
}

#[test]
fn test_matrix4_mul_translation_scale_and_alias() {
    let m1 = translation(1.0, 0.0, 0.0);
    let m2 = Matrix4::new(
        row(2.0, 0.0, 0.0, 0.0),
        row(0.0, 3.0, 0.0, 0.0),
        row(0.0, 0.0, 4.0, 0.0),
        row(0.0, 0.0, 0.0, 1.0),
    );
    let dst = m1 * &m2;
    assert_row(dst.x, 2.0, 0.0, 0.0, 0.0);
    assert_row(dst.y, 0.0, 3.0, 0.0, 0.0);
    assert_row(dst.z, 0.0, 0.0, 4.0, 0.0);
    assert_row(dst.t, 2.0, 0.0, 0.0, 1.0);
}

#[test]
fn test_matrix4_determinant() {
    assert_eq!(identity().determinant(), 1.0);
    let mut m = identity();
    m.x.x = 2.0;
    m.y.y = 4.0;
    m.z.z = 8.0;
    assert_eq!(m.determinant(), 64.0);
    let mut shear = identity();
    shear.x.y = 2.0;
    assert_eq!(shear.determinant(), 1.0);
    assert_eq!(fill(0.0).determinant(), 0.0);
    let mut small = identity();
    small.x.x = 0.125;
    small.y.y = 0.125;
    small.z.z = 0.125;
    assert_eq!(small.determinant(), 0.001953125);
    small.x.x = 0.0625;
    small.y.y = 0.0625;
    small.z.z = 0.0625;
    assert_eq!(small.determinant(), 0.0625 * 0.0625 * 0.0625);
}

#[test]
fn test_matrix4_translate_post_and_pre() {
    let m = x180();
    let v3 = Vec3::new(0.0, 1.0, 0.0);
    let post = m.translate3v(v3);
    assert_row(post.x, 1.0, 0.0, 0.0, 0.0);
    assert_row(post.y, 0.0, -1.0, 0.0, 0.0);
    assert_row(post.z, 0.0, 0.0, -1.0, 0.0);
    assert_row(post.t, 0.0, 1.0, 0.0, 1.0);

    let pre = m.translate3v_i(v3);
    assert_row(pre.x, 1.0, 0.0, 0.0, 0.0);
    assert_row(pre.y, 0.0, -1.0, 0.0, 0.0);
    assert_row(pre.z, 0.0, 0.0, -1.0, 0.0);
    assert_row(pre.t, 0.0, -1.0, 0.0, 1.0);

    let mut v3w = Vec3::new(0.0, 1.0, 0.0);
    v3w.w = 9.0;
    let post = identity().translate3v(v3w);
    assert_row(post.t, 0.0, 1.0, 0.0, 1.0);

    let v4 = Vec4::new(4.0, 5.0, 6.0, 7.0);
    let post = identity().translate4v(v4);
    assert_row(post.x, 1.0, 0.0, 0.0, 0.0);
    assert_row(post.t, 4.0, 5.0, 6.0, 7.0);

    let pre = m.translate4v_i(v4);
    assert_row(pre.y, 0.0, -1.0, 0.0, 0.0);
    assert_row(pre.t, 4.0, -5.0, -6.0, 7.0);
}

#[test]
fn test_matrix4_rotate_post_and_pre() {
    let m = translation(1.0, 2.0, 3.0);
    let q = Quat::new(1.0, 0.0, 0.0, 0.0);
    let dst = m.rotate(q);
    assert_row(dst.x, 1.0, 0.0, 0.0, 0.0);
    assert_row(dst.y, 0.0, -1.0, 0.0, 0.0);
    assert_row(dst.z, 0.0, 0.0, -1.0, 0.0);
    assert_row(dst.t, 1.0, -2.0, -3.0, 1.0);

    // rotate_i multiplies on the left and keeps the right-hand translation.
    let dst = m.rotate_i(q);
    assert_row(dst.x, 1.0, 0.0, 0.0, 0.0);
    assert_row(dst.y, 0.0, -1.0, 0.0, 0.0);
    assert_row(dst.z, 0.0, 0.0, -1.0, 0.0);
    assert_row(dst.t, 1.0, 2.0, 3.0, 1.0);

    let aa = AxisAng::new(0.0, 1.0, 0.0, 0.0);
    let dst = m.rotate_aa(aa);
    assert_row(dst.x, 1.0, 0.0, 0.0, 0.0);
    assert_row(dst.y, 0.0, 1.0, 0.0, 0.0);
    assert_row(dst.z, 0.0, 0.0, 1.0, 0.0);
    assert_row(dst.t, 1.0, 2.0, 3.0, 1.0);

    let dst = m.rotate_aa_i(aa);
    assert_row(dst.t, 1.0, 2.0, 3.0, 1.0);
    assert_row(dst.x, 1.0, 0.0, 0.0, 0.0);
}

#[test]
fn test_matrix4_scale_post_and_pre() {
    let m = translation(1.0, 2.0, 3.0);
    let mut v = Vec3::new(2.0, 3.0, 4.0);
    v.w = 5.0;
    let dst = m.scale(v);
    assert_row(dst.x, 2.0, 0.0, 0.0, 0.0);
    assert_row(dst.y, 0.0, 3.0, 0.0, 0.0);
    assert_row(dst.z, 0.0, 0.0, 4.0, 0.0);
    assert_row(dst.t, 2.0, 6.0, 12.0, 1.0);

    let dst = m.scale_i(v);
    assert_row(dst.x, 2.0, 0.0, 0.0, 0.0);
    assert_row(dst.y, 0.0, 3.0, 0.0, 0.0);
    assert_row(dst.z, 0.0, 0.0, 4.0, 0.0);
    assert_row(dst.t, 1.0, 2.0, 3.0, 1.0);
}

#[test]
fn test_matrix4_inv_scale_translation_and_singular() {
    let mut m = identity();
    m.x.x = 2.0;
    m.y.y = 4.0;
    m.z.z = 8.0;
    let dst = m.try_inv().expect("invertible scale");
    assert_row(dst.x, 0.5, 0.0, 0.0, 0.0);
    assert_row(dst.y, 0.0, 0.25, 0.0, 0.0);
    assert_row(dst.z, 0.0, 0.0, 0.125, 0.0);
    assert_row(dst.t, 0.0, 0.0, 0.0, 1.0);

    let dst = translation(4.0, -2.0, 8.0)
        .try_inv()
        .expect("invertible translation");
    assert_row(dst.x, 1.0, 0.0, 0.0, 0.0);
    assert_row(dst.y, 0.0, 1.0, 0.0, 0.0);
    assert_row(dst.z, 0.0, 0.0, 1.0, 0.0);
    assert_row(dst.t, -4.0, 2.0, -8.0, 1.0);

    let mut m = identity();
    m.x.x = 0.125;
    m.y.y = 0.125;
    m.z.z = 0.125;
    let dst = m.try_inv().expect("above the cutoff");
    assert_row(dst.x, 8.0, 0.0, 0.0, 0.0);
    assert_row(dst.y, 0.0, 8.0, 0.0, 0.0);
    assert_row(dst.z, 0.0, 0.0, 8.0, 0.0);
    assert_row(dst.t, 0.0, 0.0, 0.0, 1.0);

    // Below the cutoff the inverse is rejected and the previous dst stays.
    let mut m = identity();
    m.x.x = 0.0625;
    m.y.y = 0.0625;
    m.z.z = 0.0625;
    assert!(m.try_inv().is_none());
    assert!(fill(0.0).try_inv().is_none());
}

#[test]
fn test_matrix4_transpose() {
    let m = Matrix4::new(
        row(1.0, 2.0, 3.0, 4.0),
        row(5.0, 6.0, 7.0, 8.0),
        row(9.0, 10.0, 11.0, 12.0),
        row(13.0, 14.0, 15.0, 16.0),
    );
    let dst = m.transpose();
    assert_row(dst.x, 1.0, 5.0, 9.0, 13.0);
    assert_row(dst.y, 2.0, 6.0, 10.0, 14.0);
    assert_row(dst.z, 3.0, 7.0, 11.0, 15.0);
    assert_row(dst.t, 4.0, 8.0, 12.0, 16.0);
}
