//! Tier 1: safe-API tests mirroring `test/cmocka/test_bounds.c`.
//!
//! `bounds_get_point` returns the corner by value, so `w` is 0. C leaves the
//! destination `w` untouched; the shim test covers that. `bounds_plane_test`
//! and `bounds_under_plane` are not in the cmocka file (C reads an
//! uninitialized `w`). These tests pin the defined `w = 0` result.

use obs_c_oracle as _;
use obs_graphics::bounds::Bounds;
use obs_graphics::matrix3::Matrix3;
use obs_graphics::matrix4::Matrix4;
use obs_graphics::plane::Plane;
use obs_graphics::vec3::Vec3;
use obs_graphics::vec4::Vec4;

fn v(x: f32, y: f32, z: f32, w: f32) -> Vec3 {
    Vec3::with_w(x, y, z, w)
}

fn b(min: Vec3, max: Vec3) -> Bounds {
    Bounds::new(min, max)
}

fn assert_bounds(got: Bounds, min: Vec3, max: Vec3) {
    assert_eq!((got.min, got.max), (min, max));
}

fn identity3() -> Matrix3 {
    Matrix3::new(
        v(1.0, 0.0, 0.0, 0.0),
        v(0.0, 1.0, 0.0, 0.0),
        v(0.0, 0.0, 1.0, 0.0),
        v(0.0, 0.0, 0.0, 0.0),
    )
}

fn identity4() -> Matrix4 {
    Matrix4::new(
        Vec4::new(1.0, 0.0, 0.0, 0.0),
        Vec4::new(0.0, 1.0, 0.0, 0.0),
        Vec4::new(0.0, 0.0, 1.0, 0.0),
        Vec4::new(0.0, 0.0, 0.0, 1.0),
    )
}

#[test]
fn test_bounds_move() {
    let src = b(v(1.0, 2.0, 3.0, 7.0), v(4.0, 5.0, 6.0, 8.0));
    let moved = src.move_by(v(10.0, -1.0, 0.5, 9.0));
    assert_bounds(moved, v(11.0, 1.0, 3.5, 0.0), v(14.0, 4.0, 6.5, 0.0));
}

#[test]
fn test_bounds_scale_keeps_w() {
    let src = b(v(1.0, 2.0, 3.0, 7.0), v(4.0, 5.0, 6.0, 8.0));
    let scaled = src.scale(v(2.0, 3.0, -1.0, 5.0));
    assert_bounds(scaled, v(2.0, 6.0, -3.0, 35.0), v(8.0, 15.0, -6.0, 40.0));
}

#[test]
fn test_bounds_merge_nan_lane() {
    let a = b(v(f32::NAN, 0.0, 0.0, 1.0), v(f32::NAN, 0.0, 0.0, 1.0));
    let other = b(v(1.0, f32::NAN, 2.0, 3.0), v(1.0, 5.0, f32::NAN, 3.0));
    let merged = a.merge(other);
    assert_eq!(merged.min.x, 1.0);
    assert!(merged.min.y.is_nan());
    assert_eq!(merged.min.z, 0.0);
    assert_eq!(merged.min.w, 0.0);
    assert_eq!(merged.max.x, 1.0);
    assert_eq!(merged.max.y, 5.0);
    assert!(merged.max.z.is_nan());
    assert_eq!(merged.max.w, 0.0);
}

#[test]
fn test_bounds_merge_point() {
    let src = b(v(0.0, 0.0, 0.0, 7.0), v(2.0, 2.0, 2.0, 8.0));
    let merged = src.merge_point(v(-1.0, 5.0, 1.0, 9.0));
    assert_bounds(merged, v(-1.0, 0.0, 0.0, 0.0), v(2.0, 5.0, 2.0, 0.0));
}

#[test]
fn test_bounds_get_point() {
    let src = b(v(1.0, 2.0, 3.0, 7.0), v(4.0, 5.0, 6.0, 8.0));
    let corners = [
        v(1.0, 2.0, 3.0, 0.0),
        v(1.0, 2.0, 6.0, 0.0),
        v(1.0, 5.0, 3.0, 0.0),
        v(1.0, 5.0, 6.0, 0.0),
        v(4.0, 2.0, 3.0, 0.0),
        v(4.0, 2.0, 6.0, 0.0),
        v(4.0, 5.0, 3.0, 0.0),
        v(4.0, 5.0, 6.0, 0.0),
    ];
    for (i, corner) in corners.iter().enumerate() {
        assert_eq!(src.point(i as u32), Some(*corner));
    }
    assert_eq!(src.point(8), Some(corners[6]));
    assert_eq!(src.point(9), None);
}

#[test]
fn test_bounds_get_center() {
    let src = b(v(0.0, 2.0, 4.0, 7.0), v(2.0, 6.0, 10.0, 8.0));
    assert_eq!(src.center(), v(1.0, 4.0, 7.0, 0.0));
}

#[test]
fn test_bounds_transform_translation() {
    let src = b(v(0.0, 0.0, 0.0, 7.0), v(2.0, 4.0, 6.0, 8.0));
    let mut m = identity4();
    m.t = Vec4::new(10.0, -1.0, 0.5, 1.0);
    let got = src.transform(&m);
    assert_bounds(got, v(10.0, -1.0, 0.5, 0.0), v(12.0, 3.0, 6.5, 0.0));
}

#[test]
fn test_bounds_transform_z_rotation() {
    let src = b(v(0.0, 0.0, 0.0, 7.0), v(2.0, 4.0, 6.0, 8.0));
    let mut m = identity4();
    m.x = Vec4::new(0.0, -1.0, 0.0, 0.0);
    m.y = Vec4::new(1.0, 0.0, 0.0, 0.0);
    let got = src.transform(&m);
    assert_bounds(got, v(0.0, -2.0, 0.0, 0.0), v(4.0, 0.0, 6.0, 0.0));
}

#[test]
fn test_bounds_transform3x4_translation() {
    let src = b(v(0.0, 0.0, 0.0, 7.0), v(2.0, 4.0, 6.0, 8.0));
    let mut m = identity3();
    m.t = v(10.0, 0.0, 0.0, 9.0);
    let got = src.transform3x4(&m);
    assert_bounds(got, v(-10.0, 0.0, 0.0, 0.0), v(-8.0, 4.0, 6.0, 0.0));
}

#[test]
fn test_bounds_intersection_ray_hit() {
    let src = b(v(0.0, 0.0, 0.0, 0.0), v(2.0, 2.0, 2.0, 0.0));
    let dir = v(1.0, 0.0, 0.0, 0.0);
    assert_eq!(src.intersection_ray(v(-1.0, 1.0, 1.0, 0.0), dir), Some(1.0));
    assert_eq!(src.intersection_ray(v(1.0, 1.0, 1.0, 0.0), dir), Some(1.0));
}

#[test]
fn test_bounds_intersection_ray_miss() {
    let src = b(v(0.0, 0.0, 0.0, 0.0), v(2.0, 2.0, 2.0, 0.0));
    let orig = v(3.0, 1.0, 1.0, 0.0);
    assert_eq!(src.intersection_ray(orig, v(1.0, 0.0, 0.0, 0.0)), None);
    assert_eq!(src.intersection_ray(orig, v(0.0, 1.0, 0.0, 0.0)), None);
}

#[test]
fn test_bounds_intersection_line() {
    let src = b(v(0.0, 0.0, 0.0, 0.0), v(2.0, 2.0, 2.0, 0.0));
    assert_eq!(
        src.intersection_line(v(-1.0, 1.0, 1.0, 4.0), v(3.0, 1.0, 1.0, 0.0)),
        Some(0.25)
    );
    assert_eq!(
        src.intersection_line(v(-1.0, 1.0, 1.0, 4.0), v(-1.0, 1.0, 1.0, 0.0)),
        None
    );
    assert_eq!(
        src.intersection_line(v(3.0, 1.0, 1.0, 0.0), v(5.0, 1.0, 1.0, 0.0)),
        None
    );
}

#[test]
fn test_bounds_intersects_epsilon() {
    let a = b(v(0.0, 0.0, 0.0, 0.0), v(1.0, 1.0, 1.0, 0.0));
    let touch = b(v(1.0, 0.0, 0.0, 0.0), v(2.0, 1.0, 1.0, 0.0));
    let gap = b(v(1.5, 0.0, 0.0, 0.0), v(2.5, 1.0, 1.0, 0.0));
    assert!(a.intersects(&touch, 0.0));
    assert!(!a.intersects(&gap, 0.0));
    assert!(a.intersects(&gap, 0.5));
    assert!(!a.intersects(&gap, 0.25));
}

#[test]
fn test_bounds_intersects_obb_identity() {
    let a = b(v(0.0, 0.0, 0.0, 0.0), v(1.0, 1.0, 1.0, 0.0));
    let overlap = b(v(0.5, 0.5, 0.5, 0.0), v(1.5, 1.5, 1.5, 0.0));
    let apart = b(v(3.0, 0.0, 0.0, 0.0), v(4.0, 1.0, 1.0, 0.0));
    let m = identity4();
    assert!(a.intersects_obb(&overlap, &m, 0.0));
    assert!(!a.intersects_obb(&apart, &m, 0.0));
}

#[test]
fn test_bounds_intersects_obb3x4_identity() {
    let a = b(v(0.0, 0.0, 0.0, 0.0), v(1.0, 1.0, 1.0, 0.0));
    let overlap = b(v(0.5, 0.5, 0.5, 0.0), v(1.5, 1.5, 1.5, 0.0));
    let apart = b(v(3.0, 0.0, 0.0, 0.0), v(4.0, 1.0, 1.0, 0.0));
    let m = identity3();
    assert!(a.intersects_obb3x4(&overlap, &m, 0.0));
    assert!(!a.intersects_obb3x4(&apart, &m, 0.0));
}

#[test]
fn test_bounds_min_dist() {
    let src = b(v(0.0, 0.0, 3.0, 7.0), v(0.0, 0.0, 5.0, 8.0));
    let mut p = Plane::new(v(0.0, 0.0, 1.0, 5.0), 0.0);
    assert_eq!(src.min_dist(&p), 3.0);
    p.dist = 9.0;
    assert_eq!(src.min_dist(&p), 3.0);
}

#[test]
fn test_bounds_plane_test_w0() {
    let src = b(v(0.0, 0.0, 0.0, 0.0), v(1.0, 1.0, 1.0, 0.0));
    let inside = Plane::new(v(0.0, 0.0, 1.0, 0.0), 2.0);
    let outside = Plane::new(v(0.0, 0.0, 1.0, 0.0), -1.0);
    let partial = Plane::new(v(0.0, 0.0, 1.0, 0.0), 0.5);
    assert_eq!(src.plane_test(&inside), 2);
    assert_eq!(src.plane_test(&outside), 1);
    assert_eq!(src.plane_test(&partial), 3);
}

#[test]
fn test_bounds_under_plane_w0() {
    let src = b(v(0.0, 0.0, 0.0, 0.0), v(1.0, 1.0, 1.0, 0.0));
    assert!(src.under_plane(&Plane::new(v(0.0, 0.0, 1.0, 0.0), 0.0)));
    assert!(!src.under_plane(&Plane::new(v(0.0, 0.0, 1.0, 0.0), -1.0)));
    assert!(src.under_plane(&Plane::new(v(0.0, 0.0, -1.0, 0.0), -0.5)));
}
