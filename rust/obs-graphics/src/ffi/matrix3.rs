//! C ABI shims for the exported functions in `libobs/graphics/matrix3.h`.
//!
//! The shims only convert to the safe core in [`crate::matrix3`]. Each reads
//! its inputs before writing `dst`, so a caller may alias `dst` with an input.
//! Header-inline helpers (`matrix3_copy`, `matrix3_identity`, translate,
//! `matrix3_rotate_aa4f`, `matrix3_scale3f`) stay in the C header.

use core::ptr;

use super::axisang::axisang;
use super::matrix4::matrix4;
use super::plane::plane;
use super::quat::quat;
use super::vec3::vec3;
use crate::axisang::AxisAng;
use crate::matrix3::Matrix3;
use crate::quat::Quat;
use crate::vec3::Vec3;

/// `struct matrix3`: three axes and a translation, size 64, align 16.
#[repr(C)]
#[allow(non_camel_case_types)]
#[derive(Debug, Clone, Copy, Default, PartialEq)]
pub struct matrix3 {
    pub x: vec3,
    pub y: vec3,
    pub z: vec3,
    pub t: vec3,
}

impl From<matrix3> for Matrix3 {
    fn from(m: matrix3) -> Self {
        Self::new(m.x.into(), m.y.into(), m.z.into(), m.t.into())
    }
}

impl From<Matrix3> for matrix3 {
    fn from(m: Matrix3) -> Self {
        Self {
            x: m.x.into(),
            y: m.y.into(),
            z: m.z.into(),
            t: m.t.into(),
        }
    }
}

/// Writes the rotation matrix of `*q` to `*dst`.
///
/// # Safety
///
/// `q` must be valid for reads of a `quat` and `dst` valid for writes of a
/// `matrix3`. They may overlap.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn matrix3_from_quat(dst: *mut matrix3, q: *const quat) {
    // SAFETY: the caller guarantees `q` is readable; read before writing `dst`.
    let src = unsafe { ptr::read(q) };
    // SAFETY: the caller guarantees `dst` is writable.
    unsafe { ptr::write(dst, Matrix3::from_quat(Quat::from(src)).into()) };
}

/// Writes the rotation matrix of `*aa` to `*dst`.
///
/// # Safety
///
/// `aa` must be valid for reads of an `axisang` and `dst` valid for writes
/// of a `matrix3`. They may overlap.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn matrix3_from_axisang(dst: *mut matrix3, aa: *const axisang) {
    // SAFETY: the caller guarantees `aa` is readable; read before writing `dst`.
    let src = unsafe { ptr::read(aa) };
    let axis = AxisAng::new(src.x, src.y, src.z, src.w);
    // SAFETY: the caller guarantees `dst` is writable.
    unsafe { ptr::write(dst, Matrix3::from_axisang(axis).into()) };
}

/// Writes the upper 3x4 of `*m` to `*dst`, with every `w` lane cleared.
///
/// # Safety
///
/// `m` must be valid for reads of a `matrix4` and `dst` valid for writes of
/// a `matrix3`. They may overlap.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn matrix3_from_matrix4(dst: *mut matrix3, m: *const matrix4) {
    // SAFETY: the caller guarantees `m` is readable; read before writing `dst`.
    let src = unsafe { ptr::read(m) };
    // SAFETY: the caller guarantees `dst` is writable.
    unsafe { ptr::write(dst, Matrix3::from_matrix4(src.into()).into()) };
}

/// Writes `m1 * m2` to `*dst`.
///
/// # Safety
///
/// `m1` and `m2` must be valid for reads of a `matrix3` and `dst` valid for
/// writes. `dst` may alias either input.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn matrix3_mul(dst: *mut matrix3, m1: *const matrix3, m2: *const matrix3) {
    // SAFETY: the caller guarantees both inputs are readable; read before writing.
    let (a, b) = unsafe { (ptr::read(m1), ptr::read(m2)) };
    // SAFETY: the caller guarantees `dst` is writable.
    unsafe { ptr::write(dst, (Matrix3::from(a) * &Matrix3::from(b)).into()) };
}

/// Writes `*m` rotated by `*q` to `*dst`.
///
/// # Safety
///
/// `m` and `q` must be readable and `dst` writable. `dst` may alias `m`.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn matrix3_rotate(dst: *mut matrix3, m: *const matrix3, q: *const quat) {
    // SAFETY: the caller guarantees the inputs are readable; read before writing.
    let (src, rot) = unsafe { (ptr::read(m), ptr::read(q)) };
    // SAFETY: the caller guarantees `dst` is writable.
    unsafe { ptr::write(dst, Matrix3::from(src).rotate(Quat::from(rot)).into()) };
}

/// Writes `*m` rotated by `*aa` to `*dst`.
///
/// # Safety
///
/// `m` and `aa` must be readable and `dst` writable. `dst` may alias `m`.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn matrix3_rotate_aa(
    dst: *mut matrix3,
    m: *const matrix3,
    aa: *const axisang,
) {
    // SAFETY: the caller guarantees the inputs are readable; read before writing.
    let (src, rot) = unsafe { (ptr::read(m), ptr::read(aa)) };
    let axis = AxisAng::new(rot.x, rot.y, rot.z, rot.w);
    // SAFETY: the caller guarantees `dst` is writable.
    unsafe { ptr::write(dst, Matrix3::from(src).rotate_aa(axis).into()) };
}

/// Writes each row of `*m` scaled by `*v` to `*dst`.
///
/// # Safety
///
/// `m` and `v` must be readable and `dst` writable. `dst` may alias `m`.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn matrix3_scale(dst: *mut matrix3, m: *const matrix3, v: *const vec3) {
    // SAFETY: the caller guarantees the inputs are readable; read before writing.
    let (src, scale) = unsafe { (ptr::read(m), ptr::read(v)) };
    // SAFETY: the caller guarantees `dst` is writable.
    unsafe { ptr::write(dst, Matrix3::from(src).scale(Vec3::from(scale)).into()) };
}

/// Writes the transpose of `*m` to `*dst`.
///
/// # Safety
///
/// `m` must be readable and `dst` writable. `dst` may alias `m`.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn matrix3_transpose(dst: *mut matrix3, m: *const matrix3) {
    // SAFETY: the caller guarantees `m` is readable; read before writing `dst`.
    let src = unsafe { ptr::read(m) };
    // SAFETY: the caller guarantees `dst` is writable.
    unsafe { ptr::write(dst, Matrix3::from(src).transpose().into()) };
}

/// Writes the inverse of `*m` to `*dst`.
///
/// When the determinant is rejected, only `dst->t.w` changes, so the previous
/// contents of `dst` are part of the result.
///
/// # Safety
///
/// `m` must be readable and `dst` readable and writable. `dst` may alias `m`.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn matrix3_inv(dst: *mut matrix3, m: *const matrix3) {
    // SAFETY: the caller guarantees both are readable; read before writing `dst`.
    let (prev, src) = unsafe { (ptr::read(dst), ptr::read(m)) };
    // SAFETY: the caller guarantees `dst` is writable.
    unsafe { ptr::write(dst, Matrix3::from(prev).inv(src.into()).into()) };
}

/// Writes `*m` mirrored across `*p` to `*dst`.
///
/// # Safety
///
/// `m` and `p` must be readable and `dst` writable. `dst` may alias `m`.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn matrix3_mirror(dst: *mut matrix3, m: *const matrix3, p: *const plane) {
    // SAFETY: the caller guarantees the inputs are readable; read before writing.
    let (src, pl) = unsafe { (ptr::read(m), ptr::read(p)) };
    // SAFETY: the caller guarantees `dst` is writable.
    unsafe { ptr::write(dst, Matrix3::from(src).mirror(&pl.into()).into()) };
}

/// Writes `*m` mirrored across the direction `*v` to `*dst`.
///
/// # Safety
///
/// `m` and `v` must be readable and `dst` writable. `dst` may alias `m`.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn matrix3_mirrorv(dst: *mut matrix3, m: *const matrix3, v: *const vec3) {
    // SAFETY: the caller guarantees the inputs are readable; read before writing.
    let (src, dir) = unsafe { (ptr::read(m), ptr::read(v)) };
    // SAFETY: the caller guarantees `dst` is writable.
    unsafe { ptr::write(dst, Matrix3::from(src).mirrorv(dir.into()).into()) };
}
