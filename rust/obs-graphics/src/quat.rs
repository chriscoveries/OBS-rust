//! Safe core of `libobs/graphics/quat.c`.
//!
//! Header-inline helpers stay in `quat.h`. The functions here follow the C
//! expression order, including the SSE `quat_dot` / `vec3_dot` sum
//! `(w + y) + (z + x)`, so results are bit-identical to the C code.
//! `sin`/`cos`/`acos`/`asin`/`atan2` are the Rust `f32` methods, which match
//! the C library's `*f` functions on the Tier 3 inputs.

use core::ops::{Add, Mul, Sub};

use crate::axisang::AxisAng;
use crate::matrix3::Matrix3;
use crate::matrix4::Matrix4;
use crate::vec3::Vec3;

/// `EPSILON` from `math-defs.h`.
const EPSILON: f32 = 1e-4;

/// A quaternion with vector part (`x`, `y`, `z`) and scalar part `w`, the
/// safe counterpart of `struct quat`.
#[derive(Debug, Clone, Copy, Default, PartialEq)]
pub struct Quat {
    pub x: f32,
    pub y: f32,
    pub z: f32,
    pub w: f32,
}

impl Quat {
    #[must_use]
    pub const fn new(x: f32, y: f32, z: f32, w: f32) -> Self {
        Self { x, y, z, w }
    }

    /// Dot product over all four lanes, summed in the order of the SSE
    /// `quat_dot`.
    #[must_use]
    pub fn dot(self, other: Self) -> f32 {
        Vec3::with_w(self.x, self.y, self.z, self.w)
            .dot(Vec3::with_w(other.x, other.y, other.z, other.w))
    }

    /// Every lane times `f` (`quat_mulf`).
    #[must_use]
    pub fn mulf(self, f: f32) -> Self {
        Self::new(self.x * f, self.y * f, self.z * f, self.w * f)
    }

    /// `quat_from_axisang`.
    #[must_use]
    pub fn from_axisang(aa: AxisAng) -> Self {
        let halfa = aa.w * 0.5;
        let sine = halfa.sin();
        Self::new(aa.x * sine, aa.y * sine, aa.z * sine, halfa.cos())
    }

    /// `quat_from_matrix4`. A positive trace takes the `w` branch. Otherwise
    /// the largest diagonal element selects the lane, as in C.
    #[must_use]
    pub fn from_matrix4(m: &Matrix4) -> Self {
        Self::from_rows([
            [m.x.x, m.x.y, m.x.z, m.x.w],
            [m.y.x, m.y.y, m.y.z, m.y.w],
            [m.z.x, m.z.y, m.z.z, m.z.w],
            [m.t.x, m.t.y, m.t.z, m.t.w],
        ])
    }

    /// `quat_from_matrix3`. C casts the `matrix3` to `matrix4` and calls
    /// `quat_from_matrix4`, so the `w` lane of each axis is read.
    #[must_use]
    pub fn from_matrix3(m: &Matrix3) -> Self {
        Self::from_rows([
            [m.x.x, m.x.y, m.x.z, m.x.w],
            [m.y.x, m.y.y, m.y.z, m.y.w],
            [m.z.x, m.z.y, m.z.z, m.z.w],
            [m.t.x, m.t.y, m.t.z, m.t.w],
        ])
    }

    fn from_rows(v: [[f32; 4]; 4]) -> Self {
        let tr = (v[0][0] + v[1][1]) + v[2][2];
        if tr > 0.0 {
            let four_d = (tr + 1.0).sqrt();
            let inv_half = 0.5 / four_d;
            Self::new(
                (v[1][2] - v[2][1]) * inv_half,
                (v[2][0] - v[0][2]) * inv_half,
                (v[0][1] - v[1][0]) * inv_half,
                four_d * 0.5,
            )
        } else {
            let mut i = if v[0][0] > v[1][1] { 0 } else { 1 };
            if v[2][2] > v[i][i] {
                i = 2;
            }
            let j = (i + 1) % 3;
            let k = (i + 2) % 3;
            let four_d = (((v[i][i] - v[j][j]) - v[k][k]) + 1.0).sqrt();
            let inv_half = 0.5 / four_d;
            let mut p = [0.0; 4];
            p[i] = four_d * 0.5;
            p[j] = (v[i][j] + v[j][i]) * inv_half;
            p[k] = (v[i][k] + v[k][i]) * inv_half;
            p[3] = (v[j][k] - v[k][j]) * inv_half;
            Self::new(p[0], p[1], p[2], p[3])
        }
    }

    /// The Z axis of `matrix3_from_quat` (`quat_get_dir`).
    ///
    /// Only the Z row is needed. Its terms are the ones `matrix3.c` computes
    /// for `dst->z`, in the same order. `w` of the result is 0, as `vec3_set`
    /// leaves it.
    #[must_use]
    pub fn dir(self) -> Vec3 {
        let norm = self.dot(self);
        let s = if norm > 0.0 { 2.0 / norm } else { 0.0 };
        let xx = (self.x * self.x) * s;
        let yy = (self.y * self.y) * s;
        let xz = (self.x * self.z) * s;
        let yz = (self.y * self.z) * s;
        let wx = (self.w * self.x) * s;
        let wy = (self.w * self.y) * s;
        Vec3::new(xz + wy, yz - wx, 1.0 - (xx + yy))
    }

    /// `quat_set_look_dir`.
    ///
    /// The direction is normalized with `vec3_dot` (the `w` lane counts) and
    /// then negated with `w` cleared. A component within `EPSILON` of zero
    /// selects which of the two axis-angle rotations is applied.
    #[must_use]
    pub fn from_look_dir(dir: Vec3) -> Self {
        let dot_val = dir.dot(dir);
        let scaled = if dot_val > 0.0 {
            let s = 1.0 / dot_val.sqrt();
            Vec3::with_w(dir.x * s, dir.y * s, dir.z * s, dir.w * s)
        } else {
            Vec3::default()
        };
        let new_dir = Vec3::new(-scaled.x, -scaled.y, -scaled.z);
        let close = |a: f32| (a - 0.0).abs() <= EPSILON;
        let xz_valid = close(new_dir.x) || close(new_dir.z);
        let yz_valid = close(new_dir.y);
        let mut xz_rot = Self::new(0.0, 0.0, 0.0, 1.0);
        let mut yz_rot = Self::new(0.0, 0.0, 0.0, 1.0);
        if xz_valid {
            xz_rot = Self::from_axisang(AxisAng::new(0.0, 1.0, 0.0, new_dir.x.atan2(new_dir.z)));
        }
        if yz_valid {
            yz_rot = Self::from_axisang(AxisAng::new(-1.0, 0.0, 0.0, new_dir.y.asin()));
        }
        if !xz_valid {
            yz_rot
        } else if !yz_valid {
            xz_rot
        } else {
            xz_rot.mul(yz_rot)
        }
    }

    /// `quat_log`. The scalar is cleared. The vector is scaled by
    /// `acos(w) / sin(acos(w))` only when `|w| < 1` and that sine is at
    /// least `EPSILON`.
    #[must_use]
    pub fn log(self) -> Self {
        let angle = self.w.acos();
        let sine = angle.sin();
        let mut dst = self;
        dst.w = 0.0;
        if self.w.abs() < 1.0 && sine.abs() >= EPSILON {
            dst = dst.mulf(angle / sine);
        }
        dst
    }

    /// `quat_exp`. `x`, `y`, `z` are scaled by `sin(len) / len` (`1` when
    /// `len` is within `EPSILON` of zero) and `w` becomes `cos(len)`.
    /// `len` is `sqrt((x*x + y*y) + z*z)`, not a four-lane dot.
    #[must_use]
    pub fn exp(self) -> Self {
        let length = ((self.x * self.x) + (self.y * self.y) + (self.z * self.z)).sqrt();
        let scale = if length > EPSILON {
            length.sin() / length
        } else {
            1.0
        };
        Self::new(self.x * scale, self.y * scale, self.z * scale, length.cos())
    }

    /// `quat_interpolate`. Spherical when `acos(dot) >= EPSILON`, otherwise
    /// the linear blend `q1 + (q2 - q1) * t`.
    #[must_use]
    pub fn interpolate(self, q2: Self, t: f32) -> Self {
        let anglef = self.dot(q2).acos();
        if anglef >= EPSILON {
            let sine = anglef.sin();
            let sinei = 1.0 / sine;
            let sinet = (anglef * t).sin() * sinei;
            let sineti = (anglef * (1.0 - t)).sin() * sinei;
            self.mulf(sineti).add(q2.mulf(sinet))
        } else {
            q2.sub(self).mulf(t).add(self)
        }
    }

    /// `quat_get_tangent`: `0.5 * ((q - prev) + next - q)`.
    #[must_use]
    pub fn tangent(prev: Self, q: Self, next: Self) -> Self {
        q.sub(prev).add(next).sub(q).mulf(0.5)
    }

    /// `quat_interpolate_cubic`. The third parameter is `(2 * (1 - t)) * t`.
    #[must_use]
    pub fn interpolate_cubic(q1: Self, q2: Self, m1: Self, m2: Self, t: f32) -> Self {
        let temp1 = q1.interpolate(q2, t);
        let temp2 = m1.interpolate(m2, t);
        temp1.interpolate(temp2, (2.0 * (1.0 - t)) * t)
    }
}

/// Lane-wise sum (`quat_add`).
impl core::ops::Add for Quat {
    type Output = Self;

    fn add(self, other: Self) -> Self {
        Self::new(
            self.x + other.x,
            self.y + other.y,
            self.z + other.z,
            self.w + other.w,
        )
    }
}

/// Lane-wise difference (`quat_sub`).
impl core::ops::Sub for Quat {
    type Output = Self;

    fn sub(self, other: Self) -> Self {
        Self::new(
            self.x - other.x,
            self.y - other.y,
            self.z - other.z,
            self.w - other.w,
        )
    }
}

/// Hamilton product (`quat_mul`).
///
/// The vector part is `q1.w * q2.xyz + q2.w * q1.xyz + cross(q1.xyz, q2.xyz)`,
/// with each `vec3` step clearing `w` the way the inlines do. The scalar
/// is `q1.w * q2.w - dot(q1.xyz, q2.xyz)`, and that dot sees a zero `w`.
impl core::ops::Mul for Quat {
    type Output = Self;

    fn mul(self, other: Self) -> Self {
        let a1 = Vec3::new(self.x, self.y, self.z);
        let a2 = Vec3::new(other.x, other.y, other.z);
        let t1 = a2.mulf(self.w);
        let t2 = a1.mulf(other.w);
        let sum = Vec3::new(t1.x + t2.x, t1.y + t2.y, t1.z + t2.z);
        let cross = Vec3::new(
            a1.y * a2.z - a1.z * a2.y,
            a1.z * a2.x - a1.x * a2.z,
            a1.x * a2.y - a1.y * a2.x,
        );
        let xyz = Vec3::new(sum.x + cross.x, sum.y + cross.y, sum.z + cross.z);
        Self::new(xyz.x, xyz.y, xyz.z, (self.w * other.w) - a1.dot(a2))
    }
}
