//! C ABI shims for the exported functions in `libobs/graphics/math-extra.h`.
//!
//! The shims only convert to the safe core in [`crate::math_extra`].
//! `polar_to_norm` writes `x`, `y`, and `z` and leaves `w` alone, as C does.
//! `calc_torque` reads `v1` and `v2` before it writes `dst`.

use core::ffi::c_int;
use core::ptr;

use super::vec2::vec2;
use super::vec3::vec3;
use crate::math_extra;
use crate::vec2::Vec2;

/// Writes the cartesian form of polar `*v` to `*dst`. `w` is 0.
///
/// # Safety
///
/// `v` must be readable and `dst` writable. They may overlap.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn polar_to_cart(dst: *mut vec3, v: *const vec3) {
    // SAFETY: the caller guarantees `v` is readable; read before writing `dst`.
    let src = unsafe { ptr::read(v) };
    // SAFETY: the caller guarantees `dst` is writable.
    unsafe { ptr::write(dst, math_extra::polar_to_cart(src.into()).into()) };
}

/// Writes the polar form of `*v` to `*dst`. `w` is 0.
///
/// # Safety
///
/// `v` must be readable and `dst` writable. They may overlap.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn cart_to_polar(dst: *mut vec3, v: *const vec3) {
    // SAFETY: the caller guarantees `v` is readable; read before writing `dst`.
    let src = unsafe { ptr::read(v) };
    // SAFETY: the caller guarantees `dst` is writable.
    unsafe { ptr::write(dst, math_extra::cart_to_polar(src.into()).into()) };
}

/// Writes `(atan2(x, z), asin(y))` of `*norm` to `*dst`.
///
/// # Safety
///
/// `norm` must be readable and `dst` writable.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn norm_to_polar(dst: *mut vec2, norm: *const vec3) {
    // SAFETY: the caller guarantees `norm` is readable.
    let src = unsafe { ptr::read(norm) };
    // SAFETY: the caller guarantees `dst` is writable.
    unsafe { ptr::write(dst, math_extra::norm_to_polar(src.into()).into()) };
}

/// Writes the unit direction of polar `*polar` into `*dst`, leaving `w`.
///
/// # Safety
///
/// `polar` must be readable and `dst` writable for its `x`, `y`, and `z` lanes.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn polar_to_norm(dst: *mut vec3, polar: *const vec2) {
    // SAFETY: the caller guarantees `polar` is readable.
    let src = unsafe { ptr::read(polar) };
    let n = math_extra::polar_to_norm(Vec2::from(src));
    // SAFETY: the caller guarantees `dst`'s xyz lanes are writable. `w` stays.
    unsafe {
        (*dst).x = n.x;
        (*dst).y = n.y;
        (*dst).z = n.z;
    }
}

/// `calc_torquef`. No pointers.
#[unsafe(no_mangle)]
pub extern "C" fn calc_torquef(val1: f32, val2: f32, torque: f32, min_adjust: f32, t: f32) -> f32 {
    math_extra::calc_torquef(val1, val2, torque, min_adjust, t)
}

/// Writes the torqued step from `*v1` toward `*v2` to `*dst`.
///
/// # Safety
///
/// `v1` and `v2` must be readable and `dst` writable. They may overlap.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn calc_torque(
    dst: *mut vec3,
    v1: *const vec3,
    v2: *const vec3,
    torque: f32,
    min_adjust: f32,
    t: f32,
) {
    // SAFETY: the caller guarantees both inputs are readable. Read them
    // before writing `dst`, including when `dst` is `v1`.
    let (a, b) = unsafe { (ptr::read(v1), ptr::read(v2)) };
    let out = math_extra::calc_torque(a.into(), b.into(), torque, min_adjust, t);
    // SAFETY: the caller guarantees `dst` is writable.
    unsafe { ptr::write(dst, out.into()) };
}

/// One `rand()` draw, mapped like `math-extra.c`.
///
/// # Safety
///
/// Reads the C library `rand` state and nothing else.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rand_float(positive_only: c_int) -> f32 {
    // SAFETY: `rand` only touches the C library's state. `RAND_MAX` matches
    // the CRT libobs is linked with.
    let unit = unsafe { libc::rand() } as f64 / libc::RAND_MAX as f64;
    math_extra::rand_float(positive_only != 0, unit)
}
