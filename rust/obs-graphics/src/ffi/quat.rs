//! C ABI shims for the exported functions in `libobs/graphics/quat.h`.
//!
//! The shims only convert to the safe core in [`crate::quat`]. Each reads
//! its inputs before writing `dst`, so C callers may alias `dst` and an input.
//! Header-inline helpers stay in `quat.h`.

use core::ops::Mul;
use core::ptr;

use super::axisang::axisang;
use super::matrix3::matrix3;
use super::matrix4::matrix4;
use super::vec3::vec3;
use crate::axisang::AxisAng;
use crate::matrix3::Matrix3;
use crate::matrix4::Matrix4;
use crate::quat::Quat;
use crate::vec3::Vec3;

/// `struct quat`: a union with `__m128`, so size 16, align 16.
#[repr(C, align(16))]
#[allow(non_camel_case_types)]
#[derive(Debug, Clone, Copy, Default, PartialEq)]
pub struct quat {
    pub x: f32,
    pub y: f32,
    pub z: f32,
    pub w: f32,
}

impl From<quat> for Quat {
    fn from(q: quat) -> Self {
        Self::new(q.x, q.y, q.z, q.w)
    }
}

impl From<Quat> for quat {
    fn from(q: Quat) -> Self {
        Self {
            x: q.x,
            y: q.y,
            z: q.z,
            w: q.w,
        }
    }
}

/// Writes the product of `*q1` and `*q2` to `*dst`.
///
/// # Safety
///
/// `q1` and `q2` must be readable `quat`s and `dst` writable. They may overlap.
/// As in C, the vector is stored (which clears `w`) before the scalar reads
/// `q1->w` and `q2->w`, so an aliased `dst` contributes 0 to that product.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn quat_mul(dst: *mut quat, q1: *const quat, q2: *const quat) {
    // SAFETY: the caller guarantees both sources are readable. The axes are
    // copied before `dst` is written, as `quat_vec3` does.
    let (a, b) = unsafe { (ptr::read(q1), ptr::read(q2)) };
    let product = Quat::from(a).mul(b.into());
    // SAFETY: the caller guarantees `dst` is writable. `w` is cleared before
    // the scalar load so an overlapping `q1` or `q2` observes that store.
    unsafe {
        ptr::write(
            dst,
            quat {
                x: product.x,
                y: product.y,
                z: product.z,
                w: 0.0,
            },
        );
        let w = (*q1).w * (*q2).w - Vec3::new(a.x, a.y, a.z).dot(Vec3::new(b.x, b.y, b.z));
        (*dst).w = w;
    }
}

/// Writes the quaternion of the axis-angle `*aa` to `*dst`.
///
/// # Safety
///
/// `aa` must be a readable `axisang` and `dst` a writable `quat`.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn quat_from_axisang(dst: *mut quat, aa: *const axisang) {
    // SAFETY: the caller guarantees `aa` is readable.
    let src = unsafe { ptr::read(aa) };
    // SAFETY: the caller guarantees `dst` is writable.
    unsafe { ptr::write(dst, Quat::from_axisang(AxisAng::from(src)).into()) };
}

/// Writes the quaternion of the 3x4 matrix `*m` to `*dst`.
///
/// # Safety
///
/// `m` must be a readable `matrix3` and `dst` a writable `quat`.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn quat_from_matrix3(dst: *mut quat, m: *const matrix3) {
    // SAFETY: the caller guarantees `m` is readable.
    let src = unsafe { ptr::read(m) };
    // SAFETY: the caller guarantees `dst` is writable.
    unsafe { ptr::write(dst, Quat::from_matrix3(&Matrix3::from(src)).into()) };
}

/// Writes the quaternion of the 4x4 matrix `*m` to `*dst`.
///
/// # Safety
///
/// `m` must be a readable `matrix4` and `dst` a writable `quat`.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn quat_from_matrix4(dst: *mut quat, m: *const matrix4) {
    // SAFETY: the caller guarantees `m` is readable.
    let src = unsafe { ptr::read(m) };
    // SAFETY: the caller guarantees `dst` is writable.
    unsafe { ptr::write(dst, Quat::from_matrix4(&Matrix4::from(src)).into()) };
}

/// Writes the direction of `*q` to `*dst`.
///
/// # Safety
///
/// `q` must be a readable `quat` and `dst` a writable `vec3`.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn quat_get_dir(dst: *mut vec3, q: *const quat) {
    // SAFETY: the caller guarantees `q` is readable.
    let src = unsafe { ptr::read(q) };
    // SAFETY: the caller guarantees `dst` is writable.
    unsafe { ptr::write(dst, Quat::from(src).dir().into()) };
}

/// Writes the look rotation for `*dir` to `*dst`.
///
/// # Safety
///
/// `dir` must be a readable `vec3` and `dst` a writable `quat`.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn quat_set_look_dir(dst: *mut quat, dir: *const vec3) {
    // SAFETY: the caller guarantees `dir` is readable.
    let src = unsafe { ptr::read(dir) };
    // SAFETY: the caller guarantees `dst` is writable.
    unsafe { ptr::write(dst, Quat::from_look_dir(Vec3::from(src)).into()) };
}

/// Writes the logarithm of `*q` to `*dst`.
///
/// # Safety
///
/// `q` must be a readable `quat` and `dst` writable. They may overlap.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn quat_log(dst: *mut quat, q: *const quat) {
    // SAFETY: the caller guarantees `q` is readable; read before writing `dst`.
    let src = unsafe { ptr::read(q) };
    // SAFETY: the caller guarantees `dst` is writable.
    unsafe { ptr::write(dst, Quat::from(src).log().into()) };
}

/// Writes the exponential of `*q` to `*dst`.
///
/// # Safety
///
/// `q` must be a readable `quat` and `dst` writable. They may overlap.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn quat_exp(dst: *mut quat, q: *const quat) {
    // SAFETY: the caller guarantees `q` is readable; read before writing `dst`.
    let src = unsafe { ptr::read(q) };
    // SAFETY: the caller guarantees `dst` is writable.
    unsafe { ptr::write(dst, Quat::from(src).exp().into()) };
}

/// Writes the interpolation of `*q1` and `*q2` by `t` to `*dst`.
///
/// # Safety
///
/// `q1` and `q2` must be readable `quat`s and `dst` writable. They may overlap.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn quat_interpolate(
    dst: *mut quat,
    q1: *const quat,
    q2: *const quat,
    t: f32,
) {
    // SAFETY: the caller guarantees both sources are readable; read before writing `dst`.
    let (a, b) = unsafe { (ptr::read(q1), ptr::read(q2)) };
    // SAFETY: the caller guarantees `dst` is writable.
    unsafe { ptr::write(dst, Quat::from(a).interpolate(b.into(), t).into()) };
}

/// Writes the tangent at `*q` to `*dst`.
///
/// # Safety
///
/// `prev`, `q` and `next` must be readable `quat`s and `dst` writable.
/// They may overlap.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn quat_get_tangent(
    dst: *mut quat,
    prev: *const quat,
    q: *const quat,
    next: *const quat,
) {
    // SAFETY: the caller guarantees the sources are readable; read before writing `dst`.
    let (p, c, n) = unsafe { (ptr::read(prev), ptr::read(q), ptr::read(next)) };
    // SAFETY: the caller guarantees `dst` is writable.
    unsafe {
        ptr::write(dst, Quat::tangent(p.into(), c.into(), n.into()).into());
    }
}

/// Writes the cubic interpolation to `*dst`.
///
/// # Safety
///
/// `q1`, `q2`, `m1` and `m2` must be readable `quat`s and `dst` writable.
/// They may overlap.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn quat_interpolate_cubic(
    dst: *mut quat,
    q1: *const quat,
    q2: *const quat,
    m1: *const quat,
    m2: *const quat,
    t: f32,
) {
    // SAFETY: the caller guarantees the sources are readable; read before writing `dst`.
    let (a, b, c, d) = unsafe { (ptr::read(q1), ptr::read(q2), ptr::read(m1), ptr::read(m2)) };
    // SAFETY: the caller guarantees `dst` is writable.
    unsafe {
        ptr::write(
            dst,
            Quat::interpolate_cubic(a.into(), b.into(), c.into(), d.into(), t).into(),
        );
    }
}
