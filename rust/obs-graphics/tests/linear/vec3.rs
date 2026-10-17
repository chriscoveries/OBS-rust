//! Tier 1: safe-API tests mirroring `test/cmocka/test_vec3.c`, plus edge cases.

use obs_c_oracle as _;
use obs_graphics::matrix3::Matrix3;
use obs_graphics::matrix4::Matrix4;
use obs_graphics::plane::Plane;
use obs_graphics::vec3::Vec3;
use obs_graphics::vec4::Vec4;
use proptest as _;

/// Axes that cycle the components, and a translation of (10, 20, 30).
fn cycling_axes() -> Matrix3 {
    Matrix3::new(
        Vec3::new(0.0, 1.0, 0.0),
        Vec3::new(0.0, 0.0, 1.0),
        Vec3::new(1.0, 0.0, 0.0),
        Vec3::new(10.0, 20.0, 30.0),
    )
}

fn floor_plane() -> Plane {
    Plane::new(Vec3::new(0.0, 1.0, 0.0), 2.0)
}

#[test]
fn test_vec3_from_vec4() {
    assert_eq!(
        Vec3::from_vec4(Vec4::new(1.0, 2.0, 3.0, 4.0)),
        Vec3::new(1.0, 2.0, 3.0)
    );
}

#[test]
fn test_vec3_plane_dist() {
    assert_eq!(Vec3::new(4.0, 5.0, 6.0).plane_dist(&floor_plane()), 3.0);
    assert_eq!(Vec3::new(4.0, 2.0, 6.0).plane_dist(&floor_plane()), 0.0);
}

#[test]
fn test_vec3_rotate() {
    assert_eq!(
        Vec3::new(1.0, 2.0, 3.0).rotate(&cycling_axes()),
        Vec3::new(2.0, 3.0, 1.0)
    );
}

#[test]
fn test_vec3_transform3x4() {
    assert_eq!(
        Vec3::new(1.0, 2.0, 3.0).transform3x4(&cycling_axes()),
        Vec3::new(-18.0, -27.0, -9.0)
    );
}

#[test]
fn test_vec3_transform() {
    let m = Matrix4::new(
        Vec4::new(1.0, 0.0, 0.0, 0.0),
        Vec4::new(0.0, 1.0, 0.0, 0.0),
        Vec4::new(0.0, 0.0, 1.0, 0.0),
        Vec4::new(10.0, 20.0, 30.0, 1.0),
    );
    assert_eq!(
        Vec3::new(1.0, 2.0, 3.0).transform(&m),
        Vec3::new(11.0, 22.0, 33.0)
    );
}

#[test]
fn test_vec3_mirror() {
    assert_eq!(
        Vec3::new(4.0, 5.0, 6.0).mirror(&floor_plane()),
        Vec3::new(4.0, -1.0, 6.0)
    );
}

#[test]
fn test_vec3_mirrorv() {
    assert_eq!(
        Vec3::new(1.0, 2.0, 3.0).mirrorv(Vec3::new(1.0, 0.0, 0.0)),
        Vec3::new(-1.0, 2.0, 3.0)
    );
}

#[test]
fn rand_draws_x_then_y_then_z() {
    let mut next = [0.25_f32, 0.5, 0.75].into_iter();
    assert_eq!(
        Vec3::rand(|| next.next().unwrap()),
        Vec3::new(0.25, 0.5, 0.75)
    );
}

/// The C dot product multiplies the `w` lanes too.
#[test]
fn dot_includes_w() {
    let a = Vec3::with_w(1.0, 0.0, 0.0, 2.0);
    let b = Vec3::with_w(1.0, 0.0, 0.0, 3.0);
    assert_eq!(a.dot(b), 7.0);
    assert!(Vec3::with_w(1.0, 0.0, 0.0, f32::NAN).dot(b).is_nan());
}

/// Results clear `w` even when an input carried one.
#[test]
fn results_clear_w() {
    let v = Vec3::with_w(1.0, 2.0, 3.0, 9.0);
    assert_eq!(v.rotate(&cycling_axes()).w, 0.0);
    assert_eq!(v.transform3x4(&cycling_axes()).w, 0.0);
    assert_eq!(v.mirror(&floor_plane()).w, 0.0);
    assert_eq!(v.mirrorv(Vec3::new(1.0, 0.0, 0.0)).w, 0.0);
    assert_eq!(v.sub_xyz(v).w, 0.0);
}
