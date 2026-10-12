//! Safe core of `libobs/graphics/math-extra.c`.
//!
//! Header-inline `get_percentage` and `get_percentagei` stay in
//! `math-extra.h`. `sin`/`cos`/`asin`/`atan2` are the Rust `f32` methods,
//! which match the C library's `*f` functions on the Tier 3 inputs.
//!
//! `polar_to_cart` names its `cosf` result `sinx`, as in C, and the result
//! `w` is 0. C copies that lane uninitialized. `cart_to_polar` does the same
//! except on the within-`EPSILON` path, where C zeros `w`. `polar_to_norm`
//! does not write `w`; this function returns 0 and the shim leaves the
//! caller's lane alone.
//!
//! `vec3_close` is one-sided (`v1 - v2 < epsilon` on each of xyz, `w`
//! ignored). `calc_torque` therefore copies `v2` for some far-apart pairs.
//! The safe function reads both inputs first. The shim re-reads `v1` after
//! storing the scaled step, matching C when `dst` aliases `v1`.

use crate::vec2::Vec2;
use crate::vec3::Vec3;

/// `EPSILON` from `math-defs.h`.
const EPSILON: f32 = 1e-4;
/// `LARGE_EPSILON` from `math-defs.h`.
const LARGE_EPSILON: f32 = 1e-2;

/// `close_float` from `math-defs.h`.
fn close_float(f1: f32, f2: f32, precision: f32) -> bool {
    (f1 - f2).abs() <= precision
}

/// `vec3_close`. Each lane of `v1 - v2` must be `< epsilon`. A negative
/// difference, however large, passes.
fn vec3_close(v1: Vec3, v2: Vec3, epsilon: f32) -> bool {
    let test = v1.sub_xyz(v2);
    test.x < epsilon && test.y < epsilon && test.z < epsilon
}

/// What `calc_torque` writes. `Move` still has to add `v1`, and C does that
/// after the scaled direction is stored.
pub(crate) enum TorqueWrite {
    /// Copy `v2`, including `w` (the close path and the overshoot path).
    Copy(Vec3),
    /// `dir * adjust_dist`, with `w` 0. The caller adds `v1` after that store.
    Move(Vec3),
}

/// `polar_to_cart`.
#[must_use]
pub fn polar_to_cart(v: Vec3) -> Vec3 {
    let sinx = v.x.cos();
    let sinx_z = v.z * sinx;
    Vec3::new(sinx_z * v.y.sin(), v.z * v.x.sin(), sinx_z * v.y.cos())
}

/// `cart_to_polar`. A length within `EPSILON` is the zero vector. `w` counts
/// toward that length (`vec3_len` dots it).
#[must_use]
pub fn cart_to_polar(v: Vec3) -> Vec3 {
    let radius = v.len();
    if close_float(radius, 0.0, EPSILON) {
        Vec3::new(0.0, 0.0, 0.0)
    } else {
        Vec3::new((v.y / radius).asin(), v.x.atan2(v.z), radius)
    }
}

/// `norm_to_polar`.
#[must_use]
pub fn norm_to_polar(norm: Vec3) -> Vec2 {
    Vec2::new(norm.x.atan2(norm.z), norm.y.asin())
}

/// `polar_to_norm`. `w` is 0 here; the shim does not store it.
#[must_use]
/// The sine of one angle.
///
/// `f32::sin` and `f32::cos` of the same value are a pair a backend may fold
/// into a single `sincosf` call, whose results are rounded differently from the
/// separate calls the C port makes. Keeping them in their own non-inlined
/// bodies is what stops that fold (see also `polar_cos`).
#[inline(never)]
pub fn polar_sin(x: f32) -> f32 {
    x.sin()
}

/// The cosine of one angle, the counterpart to [`polar_sin`].
#[inline(never)]
pub fn polar_cos(x: f32) -> f32 {
    x.cos()
}

pub fn polar_to_norm(polar: Vec2) -> Vec3 {
    let sinx = polar_sin(polar.x);
    Vec3::new(
        sinx * polar_cos(polar.y),
        sinx * polar_sin(polar.y),
        polar_cos(polar.x),
    )
}

/// `calc_torquef`. Values within `EPSILON` return `val2`. Otherwise the step
/// is at least `min_adjust` and the result does not pass `val2`.
#[must_use]
pub fn calc_torquef(val1: f32, val2: f32, torque: f32, min_adjust: f32, t: f32) -> f32 {
    if close_float(val1, val2, EPSILON) {
        return val2;
    }
    let mut dist = (val2 - val1) * torque;
    let mut out = val1;
    if dist > 0.0 {
        if dist < min_adjust {
            dist = min_adjust;
        }
        out += dist * t;
        if out > val2 {
            out = val2;
        }
    } else {
        if dist > -min_adjust {
            dist = -min_adjust;
        }
        out += dist * t;
        if out < val2 {
            out = val2;
        }
    }
    out
}

pub(crate) fn calc_torque_step(
    v1: Vec3,
    v2: Vec3,
    torque: f32,
    min_adjust: f32,
    t: f32,
) -> TorqueWrite {
    if vec3_close(v1, v2, EPSILON) {
        return TorqueWrite::Copy(v2);
    }
    let line = v2.sub_xyz(v1);
    let orig_dist = line.len();
    let dir = line.mulf(1.0 / orig_dist);
    let mut torque_dist = orig_dist * torque;
    if torque_dist < min_adjust {
        torque_dist = min_adjust;
    }
    let adjust_dist = torque_dist * t;
    if adjust_dist <= (orig_dist - LARGE_EPSILON) {
        TorqueWrite::Move(dir.mulf(adjust_dist))
    } else {
        TorqueWrite::Copy(v2)
    }
}

/// `calc_torque`, reading `v1` before any write.
#[must_use]
pub fn calc_torque(v1: Vec3, v2: Vec3, torque: f32, min_adjust: f32, t: f32) -> Vec3 {
    match calc_torque_step(v1, v2, torque, min_adjust, t) {
        TorqueWrite::Copy(v) => v,
        TorqueWrite::Move(scaled) => scaled.add_xyz(v1),
    }
}

/// `rand_float` given one draw of `rand() / RAND_MAX` in `f64`.
///
/// Any `positive_only` flag yields that draw cast to `f32` (the `[0, 1]`
/// branch). Otherwise the draw is mapped to `[-1, 1]`.
#[must_use]
pub fn rand_float(positive_only: bool, unit: f64) -> f32 {
    if positive_only {
        unit as f32
    } else {
        (unit * 2.0 - 1.0) as f32
    }
}
