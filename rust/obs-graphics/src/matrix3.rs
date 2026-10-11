//! Safe core of `libobs/graphics/matrix3.c`.
//!
//! `struct matrix3` stores four `vec3` rows, each with an SSE `w` lane.
//! `matrix3_transpose` copies `z.w` into the `w` lane of every axis row.
//! `matrix3_inv` builds a `matrix4` (`t.w = 1`) and inverts it with the
//! cofactor routine from `matrix4.c`. When `|det| < 0.0005` that routine
//! writes nothing, and `matrix3_inv` only clears `t.w`. Both are matched
//! exactly. `matrix4.c` itself stays in C.

use crate::axisang::AxisAng;
use crate::matrix4::Matrix4;
use crate::plane::Plane;
use crate::quat::Quat;
use crate::vec3::Vec3;

/// `fabs(det) < 0.0005f` in `matrix4_inv`. Not `EPSILON`.
const MATRIX4_DET_MIN: f32 = 0.0005;

/// Three axes and a translation, the safe counterpart of `struct matrix3`.
#[derive(Debug, Clone, Copy, Default, PartialEq)]
pub struct Matrix3 {
    pub x: Vec3,
    pub y: Vec3,
    pub z: Vec3,
    pub t: Vec3,
}

impl Matrix3 {
    #[must_use]
    pub const fn new(x: Vec3, y: Vec3, z: Vec3, t: Vec3) -> Self {
        Self { x, y, z, t }
    }

    /// `matrix3_from_quat`. A zero norm yields the identity, as in C.
    #[must_use]
    pub fn from_quat(q: Quat) -> Self {
        let norm = q.dot(q);
        let s = if norm > 0.0 { 2.0 / norm } else { 0.0 };
        let xx = q.x * q.x * s;
        let yy = q.y * q.y * s;
        let zz = q.z * q.z * s;
        let xy = q.x * q.y * s;
        let xz = q.x * q.z * s;
        let yz = q.y * q.z * s;
        let wx = q.w * q.x * s;
        let wy = q.w * q.y * s;
        let wz = q.w * q.z * s;
        Self::new(
            Vec3::new(1.0 - (yy + zz), xy + wz, xz - wy),
            Vec3::new(xy - wz, 1.0 - (xx + zz), yz + wx),
            Vec3::new(xz + wy, yz - wx, 1.0 - (xx + yy)),
            Vec3::new(0.0, 0.0, 0.0),
        )
    }

    /// `matrix3_from_axisang`. Builds the quaternion the way `quat_from_axisang`
    /// does (`sinf` / `cosf` of half the angle), then [`Self::from_quat`].
    #[must_use]
    pub fn from_axisang(aa: AxisAng) -> Self {
        let half = aa.w * 0.5;
        let sine = half.sin();
        Self::from_quat(Quat::new(aa.x * sine, aa.y * sine, aa.z * sine, half.cos()))
    }

    /// `matrix3_from_matrix4`. Copies xyz and clears every `w` lane.
    #[must_use]
    pub fn from_matrix4(m: Matrix4) -> Self {
        Self::new(
            Vec3::new(m.x.x, m.x.y, m.x.z),
            Vec3::new(m.y.x, m.y.y, m.y.z),
            Vec3::new(m.z.x, m.z.y, m.z.z),
            Vec3::new(m.t.x, m.t.y, m.t.z),
        )
    }

    /// `matrix3_rotate`.
    #[must_use]
    pub fn rotate(self, q: Quat) -> Self {
        self * &Self::from_quat(q)
    }

    /// `matrix3_rotate_aa`.
    #[must_use]
    pub fn rotate_aa(self, aa: AxisAng) -> Self {
        self * &Self::from_axisang(aa)
    }

    /// `matrix3_scale`. Each row is a lane-wise product, `w` included.
    #[must_use]
    pub fn scale(self, v: Vec3) -> Self {
        Self::new(
            self.x.lane_mul(v),
            self.y.lane_mul(v),
            self.z.lane_mul(v),
            self.t.lane_mul(v),
        )
    }

    /// `matrix3_transpose`.
    ///
    /// The 3x3 is the transpose. Every axis `w` becomes the source `z.w`,
    /// which is what the SSE shuffles in `matrix3.c` store. The translation
    /// is `vec3_rotate` then `vec3_neg` (that negation clears `w`).
    #[must_use]
    pub fn transpose(self) -> Self {
        let t = self.t.rotate(&self).neg_xyz();
        Self::new(
            Vec3::with_w(self.x.x, self.y.x, self.z.x, self.z.w),
            Vec3::with_w(self.x.y, self.y.y, self.z.y, self.z.w),
            Vec3::with_w(self.x.z, self.y.z, self.z.z, self.z.w),
            t,
        )
    }

    /// `matrix3_inv`. `self` is the previous contents of `dst`.
    ///
    /// On success the result is the `matrix4` inverse with `t.w` forced to 0,
    /// including whatever `w` lanes the cofactors produced. On failure `self`
    /// is returned with only `t.w` cleared.
    #[must_use]
    pub fn inv(self, m: Self) -> Self {
        let mut mf = m.to_lanes();
        mf[15] = 1.0;
        let det = det4(&mf);
        if det.abs() < MATRIX4_DET_MIN {
            let mut kept = self;
            kept.t.w = 0.0;
            return kept;
        }
        let mut out = [0.0; 16];
        for i in 0..4 {
            for j in 0..4 {
                let sign = 1.0 - (((i + j) % 2) as f32) * 2.0;
                out[j * 4 + i] = det3(&submatrix(&mf, i, j)) * sign / det;
            }
        }
        let mut dst = Self::from_lanes(out);
        dst.t.w = 0.0;
        dst
    }

    /// `matrix3_mirror`. Axes use `vec3_mirrorv` on the plane normal.
    /// Translation uses `vec3_mirror` on the whole plane.
    #[must_use]
    pub fn mirror(self, p: &Plane) -> Self {
        Self::new(
            self.x.mirrorv(p.dir),
            self.y.mirrorv(p.dir),
            self.z.mirrorv(p.dir),
            self.t.mirror(p),
        )
    }

    /// `matrix3_mirrorv`.
    #[must_use]
    pub fn mirrorv(self, v: Vec3) -> Self {
        Self::new(
            self.x.mirrorv(v),
            self.y.mirrorv(v),
            self.z.mirrorv(v),
            self.t.mirrorv(v),
        )
    }

    fn to_lanes(self) -> [f32; 16] {
        let rows = [self.x, self.y, self.z, self.t];
        let mut mf = [0.0; 16];
        for (i, row) in rows.iter().enumerate() {
            mf[i * 4] = row.x;
            mf[i * 4 + 1] = row.y;
            mf[i * 4 + 2] = row.z;
            mf[i * 4 + 3] = row.w;
        }
        mf
    }

    fn from_lanes(mf: [f32; 16]) -> Self {
        let row = |i: usize| Vec3::with_w(mf[i * 4], mf[i * 4 + 1], mf[i * 4 + 2], mf[i * 4 + 3]);
        Self::new(row(0), row(1), row(2), row(3))
    }
}

impl core::ops::Mul<&Self> for Matrix3 {
    type Output = Self;

    /// `matrix3_mul`. `self` is `m1`.
    fn mul(self, m2: &Self) -> Self {
        Self::new(
            self.x.rotate(m2),
            self.y.rotate(m2),
            self.z.rotate(m2),
            self.t.transform3x4(m2),
        )
    }
}

/// Minor of `m` with row `drop_row` and column `drop_col` removed, row-major.
/// Same index walk as `get_3x3_submatrix`.
fn submatrix(mf: &[f32; 16], drop_row: usize, drop_col: usize) -> [f32; 9] {
    let mut dst = [0.0; 9];
    for ti in 0..4 {
        let idst = if ti < drop_row {
            ti
        } else if ti > drop_row {
            ti - 1
        } else {
            continue;
        };
        for tj in 0..4 {
            let jdst = if tj < drop_col {
                tj
            } else if tj > drop_col {
                tj - 1
            } else {
                continue;
            };
            dst[idst * 3 + jdst] = mf[ti * 4 + tj];
        }
    }
    dst
}

/// `get_3x3_determinant`, same parenthesization.
fn det3(m: &[f32; 9]) -> f32 {
    (m[0] * ((m[4] * m[8]) - (m[7] * m[5]))) - (m[1] * ((m[3] * m[8]) - (m[6] * m[5])))
        + (m[2] * ((m[3] * m[7]) - (m[6] * m[4])))
}

/// `matrix4_determinant`. The sign flips each column, starting at `+1`.
fn det4(mf: &[f32; 16]) -> f32 {
    let mut result = 0.0;
    let mut sign = 1.0f32;
    for n in 0..4 {
        result += mf[n] * det3(&submatrix(mf, 0, n)) * sign;
        sign = -sign;
    }
    result
}
