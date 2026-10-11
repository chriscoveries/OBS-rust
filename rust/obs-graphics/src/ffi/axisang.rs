//! C ABI shim for the exported function in `libobs/graphics/axisang.h`.
//!
//! The shim only converts to the safe core in [`crate::axisang`]. It reads
//! its input before writing `dst`.

use core::ptr;

use super::quat::quat;
use crate::axisang::AxisAng;

/// `struct axisang`. Unlike `vec4`, the C union has no `__m128` member,
/// only `float ptr[4]`, so it is size 16, align 4.
#[repr(C)]
#[allow(non_camel_case_types)]
#[derive(Debug, Clone, Copy, Default, PartialEq)]
pub struct axisang {
    pub x: f32,
    pub y: f32,
    pub z: f32,
    pub w: f32,
}

impl From<axisang> for AxisAng {
    fn from(a: axisang) -> Self {
        Self::new(a.x, a.y, a.z, a.w)
    }
}

impl From<AxisAng> for axisang {
    fn from(a: AxisAng) -> Self {
        Self {
            x: a.x,
            y: a.y,
            z: a.z,
            w: a.w,
        }
    }
}

/// Writes the axis and angle of the rotation `*q` to `*dst`.
///
/// # Safety
///
/// `q` must be valid for reads of a `quat` and `dst` valid for writes of an
/// `axisang`. They may overlap.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn axisang_from_quat(dst: *mut axisang, q: *const quat) {
    // SAFETY: the caller guarantees `q` is readable; read before writing `dst`.
    let src = unsafe { ptr::read(q) };
    // SAFETY: the caller guarantees `dst` is writable.
    unsafe { ptr::write(dst, AxisAng::from_quat(src.into()).into()) };
}
