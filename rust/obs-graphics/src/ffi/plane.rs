//! C ABI shims for the exported functions in `libobs/graphics/plane.h`.
//!
//! The shims only convert to the safe core in [`crate::plane`]. Each reads
//! its inputs before writing `dst`, including `plane_from_tri` when `v1` is
//! `&dst->dir`. Header-inline helpers (`plane_copy`, `plane_set`,
//! `plane_setf`, `plane_close`, `plane_coplanar`) stay in the C header.

use core::ptr;

use super::matrix3::matrix3;
use super::matrix4::matrix4;
use super::vec3::vec3;
use crate::matrix3::Matrix3;
use crate::matrix4::Matrix4;
use crate::plane::Plane;
use crate::vec3::Vec3;

/// `struct plane`: a `vec3` direction and a distance, size 32, align 16.
#[repr(C)]
#[allow(non_camel_case_types)]
#[derive(Debug, Clone, Copy, Default, PartialEq)]
pub struct plane {
    pub dir: vec3,
    pub dist: f32,
}

impl From<plane> for Plane {
    fn from(p: plane) -> Self {
        Self::new(p.dir.into(), p.dist)
    }
}

impl From<Plane> for plane {
    fn from(p: Plane) -> Self {
        Self {
            dir: p.dir.into(),
            dist: p.dist,
        }
    }
}

/// Writes the plane through `*v1`, `*v2` and `*v3` to `*dst`.
///
/// # Safety
///
/// `v1`, `v2` and `v3` must be readable `vec3`s and `dst` writable. They may
/// overlap: all three are read in full before `dst` is written, so a `v1`
/// that is `&dst->dir` contributes its original value to the distance.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn plane_from_tri(
    dst: *mut plane,
    v1: *const vec3,
    v2: *const vec3,
    v3: *const vec3,
) {
    // SAFETY: the caller guarantees all three inputs are readable.
    let (a, b, c) = unsafe { (ptr::read(v1), ptr::read(v2), ptr::read(v3)) };
    // SAFETY: the caller guarantees `dst` is writable. The plane is built
    // from the copies before the single store.
    unsafe {
        ptr::write(
            dst,
            Plane::from_tri(Vec3::from(a), Vec3::from(b), Vec3::from(c)).into(),
        )
    };
}

/// Writes `*p` transformed by `*m` to `*dst`.
///
/// # Safety
///
/// `p` and `m` must be readable and `dst` writable. `dst` may alias `p`.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn plane_transform(dst: *mut plane, p: *const plane, m: *const matrix4) {
    // SAFETY: the caller guarantees the inputs are readable; read before writing.
    let (src, mat) = unsafe { (ptr::read(p), ptr::read(m)) };
    let out = Plane::from(src).transform(&Matrix4::from(mat));
    // SAFETY: the caller guarantees `dst` is writable.
    unsafe { ptr::write(dst, out.into()) };
}

/// Writes `*p` transformed by the 3x4 `*m` to `*dst`.
///
/// # Safety
///
/// `p` and `m` must be readable and `dst` writable. `dst` may alias `p`.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn plane_transform3x4(dst: *mut plane, p: *const plane, m: *const matrix3) {
    // SAFETY: the caller guarantees the inputs are readable; read before writing.
    let (src, mat) = unsafe { (ptr::read(p), ptr::read(m)) };
    let out = Plane::from(src).transform3x4(&Matrix3::from(mat));
    // SAFETY: the caller guarantees `dst` is writable.
    unsafe { ptr::write(dst, out.into()) };
}

/// Writes the ray parameter to `*t`. A miss stores 0.
///
/// # Safety
///
/// `p`, `orig`, and `dir` must be readable and `t` writable.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn plane_intersection_ray(
    p: *const plane,
    orig: *const vec3,
    dir: *const vec3,
    t: *mut f32,
) -> bool {
    // SAFETY: the caller guarantees the inputs are readable.
    let (plane, o, d) = unsafe { (ptr::read(p), ptr::read(orig), ptr::read(dir)) };
    let (hit, value) = Plane::from(plane).intersection_ray(Vec3::from(o), Vec3::from(d));
    // SAFETY: the caller guarantees `t` is writable. C always stores it.
    unsafe { ptr::write(t, value) };
    hit
}

/// Writes the segment parameter to `*t` only on a hit.
///
/// # Safety
///
/// `p`, `v1`, and `v2` must be readable. `t` must be writable when the
/// segment hits the plane. A miss does not read or write `t`.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn plane_intersection_line(
    p: *const plane,
    v1: *const vec3,
    v2: *const vec3,
    t: *mut f32,
) -> bool {
    // SAFETY: the caller guarantees the inputs are readable.
    let (plane, a, b) = unsafe { (ptr::read(p), ptr::read(v1), ptr::read(v2)) };
    match Plane::from(plane).intersection_line(Vec3::from(a), Vec3::from(b)) {
        Some(value) => {
            // SAFETY: the caller guarantees `t` is writable on a hit.
            unsafe { ptr::write(t, value) };
            true
        }
        None => false,
    }
}

/// `plane_tri_inside`.
///
/// # Safety
///
/// Every pointer must be readable.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn plane_tri_inside(
    p: *const plane,
    v1: *const vec3,
    v2: *const vec3,
    v3: *const vec3,
    precision: f32,
) -> bool {
    // SAFETY: the caller guarantees every pointer is readable.
    let (plane, a, b, c) = unsafe { (ptr::read(p), ptr::read(v1), ptr::read(v2), ptr::read(v3)) };
    Plane::from(plane).tri_inside(a.into(), b.into(), c.into(), precision)
}

/// `plane_line_inside`.
///
/// # Safety
///
/// Every pointer must be readable.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn plane_line_inside(
    p: *const plane,
    v1: *const vec3,
    v2: *const vec3,
    precision: f32,
) -> bool {
    // SAFETY: the caller guarantees every pointer is readable.
    let (plane, a, b) = unsafe { (ptr::read(p), ptr::read(v1), ptr::read(v2)) };
    Plane::from(plane).line_inside(a.into(), b.into(), precision)
}
