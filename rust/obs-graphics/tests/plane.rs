//! Tier 1: safe-API tests mirroring `test/cmocka/test_plane.c`.

use obs_c_oracle as _;
use obs_graphics::matrix3::Matrix3;
use obs_graphics::matrix4::Matrix4;
use obs_graphics::plane::Plane;
use obs_graphics::vec3::Vec3;
use obs_graphics::vec4::Vec4;

fn v(x: f32, y: f32, z: f32, w: f32) -> Vec3 {
    Vec3::with_w(x, y, z, w)
}

fn assert_plane(p: Plane, x: f32, y: f32, z: f32, w: f32, dist: f32) {
    assert_eq!(
        (p.dir.x, p.dir.y, p.dir.z, p.dir.w, p.dist),
        (x, y, z, w, dist)
    );
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
fn test_plane_from_tri_xy() {
    let p = Plane::from_tri(
        v(0.0, 0.0, 0.0, 0.0),
        v(1.0, 0.0, 0.0, 0.0),
        v(0.0, 1.0, 0.0, 0.0),
    );
    assert_plane(p, 0.0, 0.0, 1.0, 0.0, 0.0);
}

#[test]
fn test_plane_from_tri_offset() {
    let p = Plane::from_tri(
        v(0.0, 0.0, 1.0, 0.0),
        v(1.0, 0.0, 1.0, 0.0),
        v(0.0, 1.0, 1.0, 0.0),
    );
    assert_plane(p, 0.0, 0.0, 1.0, 0.0, 1.0);
}

#[test]
fn test_plane_from_tri_degenerate() {
    let p = Plane::from_tri(
        v(0.0, 0.0, 0.0, 0.0),
        v(1.0, 0.0, 0.0, 0.0),
        v(2.0, 0.0, 0.0, 0.0),
    );
    assert_plane(p, 0.0, 0.0, 0.0, 0.0, 0.0);
}

/// Same inputs as the C alias test, passed by value. dist is `v1·dir`
/// (`1/sqrt(3)`), and the shim test covers `v1 == &dst->dir`, where the
/// distance is the same dot.
#[test]
fn test_plane_from_tri_tilted() {
    let p = Plane::from_tri(
        v(0.0, 0.0, 1.0, 0.0),
        v(1.0, 0.0, 0.0, 0.0),
        v(0.0, 1.0, 0.0, 0.0),
    );
    assert_plane(p, 0.57735026, 0.57735026, 0.57735026, 0.0, 0.57735026);
}

#[test]
fn test_plane_from_tri_nan_w() {
    let p = Plane::from_tri(
        v(0.0, 0.0, 0.0, f32::NAN),
        v(1.0, 0.0, 0.0, 0.0),
        v(0.0, 1.0, 0.0, 0.0),
    );
    assert_eq!((p.dir.x, p.dir.y, p.dir.z, p.dir.w), (0.0, 0.0, 1.0, 0.0));
    assert!(p.dist.is_nan());
}

#[test]
fn test_plane_transform_translation() {
    let mut m = identity4();
    m.t = Vec4::new(0.0, 0.0, 3.0, 1.0);
    let p = Plane::new(v(0.0, 0.0, 1.0, 0.0), 1.0).transform(&m);
    assert_plane(p, 0.0, 0.0, 1.0, 0.0, -2.0);
}

#[test]
fn test_plane_transform_rotates_normal() {
    let mut m = identity4();
    m.y = Vec4::new(0.0, 0.0, 1.0, 0.0);
    m.z = Vec4::new(0.0, -1.0, 0.0, 0.0);
    let p = Plane::new(v(0.0, 0.0, 1.0, 0.0), 4.0).transform(&m);
    assert_plane(p, 0.0, -1.0, 0.0, 0.0, 4.0);
}

#[test]
fn test_plane_transform3x4_flips_normal() {
    let mut m = identity3();
    m.t = v(0.0, 0.0, 5.0, 0.0);
    let p = Plane::new(v(0.0, 0.0, 1.0, 0.0), 1.0).transform3x4(&m);
    assert_plane(p, 0.0, 0.0, -1.0, 0.0, 1.0);
}

#[test]
fn test_plane_transform3x4_zero_normal() {
    let mut m = identity3();
    m.t = v(1.0, 0.0, 0.0, 0.0);
    let p = Plane::new(v(1.0, 0.0, 0.0, 0.0), 4.0).transform3x4(&m);
    assert_plane(p, 0.0, 0.0, 0.0, 0.0, 4.0);
}

#[test]
fn test_plane_intersection_ray_hit() {
    let (hit, t) = Plane::new(v(0.0, 0.0, 1.0, 0.0), 4.0)
        .intersection_ray(v(0.0, 0.0, 0.0, 0.0), v(0.0, 0.0, 1.0, 0.0));
    assert!(hit);
    assert_eq!(t, 4.0);
}

#[test]
fn test_plane_intersection_ray_behind() {
    let (hit, t) = Plane::new(v(0.0, 0.0, 1.0, 0.0), 4.0)
        .intersection_ray(v(0.0, 0.0, 10.0, 0.0), v(0.0, 0.0, 1.0, 0.0));
    assert!(hit);
    assert_eq!(t, -6.0);
}

#[test]
fn test_plane_intersection_ray_epsilon() {
    let plane = Plane::new(v(1.0, 0.0, 0.0, 0.0), 1.0);
    let orig = v(0.0, 0.0, 0.0, 0.0);
    let (hit, t) = plane.intersection_ray(orig, v(0.0, 1.0, 0.0, 0.0));
    assert!(!hit);
    assert_eq!(t, 0.0);
    let (hit, t) = plane.intersection_ray(orig, v(0.00005, 0.0, 0.0, 0.0));
    assert!(!hit);
    assert_eq!(t, 0.0);
    let (hit, t) = plane.intersection_ray(orig, v(1e-4, 0.0, 0.0, 0.0));
    assert!(hit);
    assert_eq!(t, 10000.0);
}

#[test]
fn test_plane_intersection_line_crosses() {
    let plane = Plane::new(v(0.0, 0.0, 1.0, 0.0), 1.0);
    assert_eq!(
        plane.intersection_line(v(0.0, 0.0, 0.0, 0.0), v(0.0, 0.0, 2.0, 0.0)),
        Some(0.5)
    );
    assert_eq!(
        plane.intersection_line(v(0.0, 0.0, 1.0, 0.0), v(0.0, 0.0, 3.0, 0.0)),
        Some(0.0)
    );
    assert_eq!(
        plane.intersection_line(v(0.0, 0.0, 3.0, 0.0), v(0.0, 0.0, 1.0, 0.0)),
        Some(1.0)
    );
}

#[test]
fn test_plane_intersection_line_miss_leaves_t() {
    let plane = Plane::new(v(0.0, 0.0, 1.0, 0.0), 1.0);
    assert_eq!(
        plane.intersection_line(v(0.0, 0.0, 1.0, 0.0), v(1.0, 0.0, 1.0, 0.0)),
        None
    );
    assert_eq!(
        plane.intersection_line(v(0.0, 0.0, 2.0, 0.0), v(0.0, 0.0, 4.0, 0.0)),
        None
    );
}

#[test]
fn test_plane_intersection_line_w_lane() {
    let plane = Plane::new(v(0.0, 0.0, 1.0, 2.0), 6.0);
    assert_eq!(
        plane.intersection_line(v(0.0, 0.0, 0.0, 3.0), v(0.0, 0.0, 2.0, 0.0)),
        Some(0.0)
    );
}

#[test]
fn test_plane_tri_inside_slab() {
    let plane = Plane::new(v(0.0, 0.0, 1.0, 0.0), 0.0);
    let a = v(0.0, 0.0, 0.0, 0.0);
    let b = v(1.0, 0.0, 0.05, 0.0);
    let c = v(0.0, 1.0, -0.05, 0.0);
    assert!(!plane.tri_inside(a, b, c, 0.1));
    assert!(plane.tri_inside(a, b, v(0.0, 0.0, 0.1, 0.0), 0.1));
    assert!(plane.tri_inside(a, b, v(0.0, 0.0, -0.1, 0.0), 0.1));
}

#[test]
fn test_plane_line_inside_slab() {
    let plane = Plane::new(v(0.0, 0.0, 1.0, 0.0), 0.0);
    let a = v(0.0, 0.0, 0.05, 0.0);
    assert!(!plane.line_inside(a, v(1.0, 0.0, -0.05, 0.0), 0.1));
    assert!(plane.line_inside(a, v(1.0, 0.0, 0.1, 0.0), 0.1));
}
