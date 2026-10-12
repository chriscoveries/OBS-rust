//! Tier 3: the Rust C ABI shim and safe core behave exactly like the
//! original C, compiled as an oracle.
//!
//! No intentional differences from C.
//!
//! `quat_dot` and the `vec3` inlines `quat.c` calls sum in the SSE order
//! `(w + y) + (z + x)`. `quat_from_matrix3` reads a `matrix3` as a
//! `matrix4`, including each axis `w` lane. `quat_get_dir` is the Z axis
//! of `matrix3_from_quat`. Trig is `f32`'s libm, matching `sinf`/`cosf`/
//! `acosf`/`asinf`/`atan2f`. The C oracle is built with `-ffp-contract=off`.
//!
//! Reminder: the mutation check (break the core, see this file fail) is done
//! once per port, not on every change.

use core::ops::Mul;

use obs_c_oracle::graphics_math::{
    self as c, OracleAxisAng, OracleMatrix3, OracleMatrix4, OracleQuat, OracleVec3,
};
use obs_graphics::axisang::AxisAng;
use obs_graphics::ffi::axisang::axisang;
use obs_graphics::ffi::matrix3::matrix3;
use obs_graphics::ffi::matrix4::matrix4;
use obs_graphics::ffi::quat::{self as rs, quat};
use obs_graphics::ffi::vec3::vec3;
use obs_graphics::matrix3::Matrix3;
use obs_graphics::matrix4::Matrix4;
use obs_graphics::quat::Quat;
use obs_graphics::vec3::Vec3;
use proptest::prelude::*;

/// Bit-exact equality, except that any NaN equals any NaN.
fn same(a: f32, b: f32) -> bool {
    a.to_bits() == b.to_bits() || (a.is_nan() && b.is_nan())
}

fn same_q(r: quat, c: OracleQuat) -> bool {
    same(r.x, c.x) && same(r.y, c.y) && same(r.z, c.z) && same(r.w, c.w)
}

fn same_v(r: vec3, c: OracleVec3) -> bool {
    same(r.x, c.x) && same(r.y, c.y) && same(r.z, c.z) && same(r.w, c.w)
}

fn pair(q: [f32; 4]) -> (quat, OracleQuat) {
    (
        quat {
            x: q[0],
            y: q[1],
            z: q[2],
            w: q[3],
        },
        OracleQuat {
            x: q[0],
            y: q[1],
            z: q[2],
            w: q[3],
        },
    )
}

fn aa_pair(q: [f32; 4]) -> (axisang, OracleAxisAng) {
    (
        axisang {
            x: q[0],
            y: q[1],
            z: q[2],
            w: q[3],
        },
        OracleAxisAng {
            x: q[0],
            y: q[1],
            z: q[2],
            w: q[3],
        },
    )
}

fn v_pair(q: [f32; 4]) -> (vec3, OracleVec3) {
    (
        vec3 {
            x: q[0],
            y: q[1],
            z: q[2],
            w: q[3],
        },
        OracleVec3 {
            x: q[0],
            y: q[1],
            z: q[2],
            w: q[3],
        },
    )
}

fn m4_pair(m: [[f32; 4]; 4]) -> (matrix4, OracleMatrix4) {
    let row = |r: [f32; 4]| vec4_of(r);
    let orow = |r: [f32; 4]| obs_c_oracle::graphics_math::OracleVec4 {
        x: r[0],
        y: r[1],
        z: r[2],
        w: r[3],
    };
    (
        matrix4 {
            x: row(m[0]),
            y: row(m[1]),
            z: row(m[2]),
            t: row(m[3]),
        },
        OracleMatrix4 {
            x: orow(m[0]),
            y: orow(m[1]),
            z: orow(m[2]),
            t: orow(m[3]),
        },
    )
}

fn vec4_of(r: [f32; 4]) -> obs_graphics::ffi::vec4::vec4 {
    obs_graphics::ffi::vec4::vec4 {
        x: r[0],
        y: r[1],
        z: r[2],
        w: r[3],
    }
}

fn m3_pair(m: [[f32; 4]; 4]) -> (matrix3, OracleMatrix3) {
    let row = |r: [f32; 4]| vec3 {
        x: r[0],
        y: r[1],
        z: r[2],
        w: r[3],
    };
    let orow = |r: [f32; 4]| OracleVec3 {
        x: r[0],
        y: r[1],
        z: r[2],
        w: r[3],
    };
    (
        matrix3 {
            x: row(m[0]),
            y: row(m[1]),
            z: row(m[2]),
            t: row(m[3]),
        },
        OracleMatrix3 {
            x: orow(m[0]),
            y: orow(m[1]),
            z: orow(m[2]),
            t: orow(m[3]),
        },
    )
}

fn check_mul(a: [f32; 4], b: [f32; 4]) -> Result<(), TestCaseError> {
    let (ra, ca) = pair(a);
    let (rb, cb) = pair(b);
    let mut rd = quat::default();
    let mut cd = OracleQuat::default();
    // SAFETY: all pointers refer to live locals.
    unsafe {
        rs::quat_mul(&mut rd, &ra, &rb);
        c::oracle_quat_mul(&mut cd, &ca, &cb);
    }
    prop_assert!(same_q(rd, cd), "shim {rd:?} != C {cd:?} for {a:?} {b:?}");
    let core: quat = Quat::from(ra).mul(rb.into()).into();
    prop_assert!(same_q(core, cd), "core {core:?} != C {cd:?}");
    Ok(())
}

fn check_axisang(a: [f32; 4]) -> Result<(), TestCaseError> {
    let (ra, ca) = aa_pair(a);
    let mut rd = quat::default();
    let mut cd = OracleQuat::default();
    // SAFETY: all pointers refer to live locals.
    unsafe {
        rs::quat_from_axisang(&mut rd, &ra);
        c::oracle_quat_from_axisang(&mut cd, &ca);
    }
    prop_assert!(same_q(rd, cd), "shim {rd:?} != C {cd:?} for {a:?}");
    let core: quat = Quat::from_axisang(AxisAng::from(ra)).into();
    prop_assert!(same_q(core, cd), "core {core:?} != C {cd:?}");
    Ok(())
}

fn check_m4(m: [[f32; 4]; 4]) -> Result<(), TestCaseError> {
    let (rm, cm) = m4_pair(m);
    let mut rd = quat::default();
    let mut cd = OracleQuat::default();
    // SAFETY: all pointers refer to live locals.
    unsafe {
        rs::quat_from_matrix4(&mut rd, &rm);
        c::oracle_quat_from_matrix4(&mut cd, &cm);
    }
    prop_assert!(same_q(rd, cd), "shim {rd:?} != C {cd:?}");
    let core: quat = Quat::from_matrix4(&Matrix4::from(rm)).into();
    prop_assert!(same_q(core, cd), "core {core:?} != C {cd:?}");
    Ok(())
}

fn check_m3(m: [[f32; 4]; 4]) -> Result<(), TestCaseError> {
    let (rm, cm) = m3_pair(m);
    let mut rd = quat::default();
    let mut cd = OracleQuat::default();
    // SAFETY: all pointers refer to live locals.
    unsafe {
        rs::quat_from_matrix3(&mut rd, &rm);
        c::oracle_quat_from_matrix3(&mut cd, &cm);
    }
    prop_assert!(same_q(rd, cd), "shim {rd:?} != C {cd:?}");
    let core: quat = Quat::from_matrix3(&Matrix3::from(rm)).into();
    prop_assert!(same_q(core, cd), "core {core:?} != C {cd:?}");
    Ok(())
}

fn check_dir(q: [f32; 4]) -> Result<(), TestCaseError> {
    let (rq, cq) = pair(q);
    let mut rd = vec3::default();
    let mut cd = OracleVec3::default();
    // SAFETY: all pointers refer to live locals.
    unsafe {
        rs::quat_get_dir(&mut rd, &rq);
        c::oracle_quat_get_dir(&mut cd, &cq);
    }
    prop_assert!(same_v(rd, cd), "shim {rd:?} != C {cd:?} for {q:?}");
    let core: vec3 = Quat::from(rq).dir().into();
    prop_assert!(same_v(core, cd), "core {core:?} != C {cd:?}");
    Ok(())
}

fn check_look(d: [f32; 4]) -> Result<(), TestCaseError> {
    let (rv, cv) = v_pair(d);
    let mut rd = quat::default();
    let mut cd = OracleQuat::default();
    // SAFETY: all pointers refer to live locals.
    unsafe {
        rs::quat_set_look_dir(&mut rd, &rv);
        c::oracle_quat_set_look_dir(&mut cd, &cv);
    }
    prop_assert!(same_q(rd, cd), "shim {rd:?} != C {cd:?} for {d:?}");
    let core: quat = Quat::from_look_dir(Vec3::from(rv)).into();
    prop_assert!(same_q(core, cd), "core {core:?} != C {cd:?}");
    Ok(())
}

fn check_log(q: [f32; 4]) -> Result<(), TestCaseError> {
    let (rq, cq) = pair(q);
    let mut rd = quat::default();
    let mut cd = OracleQuat::default();
    // SAFETY: all pointers refer to live locals.
    unsafe {
        rs::quat_log(&mut rd, &rq);
        c::oracle_quat_log(&mut cd, &cq);
    }
    prop_assert!(same_q(rd, cd), "shim {rd:?} != C {cd:?} for {q:?}");
    let core: quat = Quat::from(rq).log().into();
    prop_assert!(same_q(core, cd), "core {core:?} != C {cd:?}");
    Ok(())
}

fn check_exp(q: [f32; 4]) -> Result<(), TestCaseError> {
    let (rq, cq) = pair(q);
    let mut rd = quat::default();
    let mut cd = OracleQuat::default();
    // SAFETY: all pointers refer to live locals.
    unsafe {
        rs::quat_exp(&mut rd, &rq);
        c::oracle_quat_exp(&mut cd, &cq);
    }
    prop_assert!(same_q(rd, cd), "shim {rd:?} != C {cd:?} for {q:?}");
    let core: quat = Quat::from(rq).exp().into();
    prop_assert!(same_q(core, cd), "core {core:?} != C {cd:?}");
    Ok(())
}

fn check_interp(a: [f32; 4], b: [f32; 4], t: f32) -> Result<(), TestCaseError> {
    let (ra, ca) = pair(a);
    let (rb, cb) = pair(b);
    let mut rd = quat::default();
    let mut cd = OracleQuat::default();
    // SAFETY: all pointers refer to live locals.
    unsafe {
        rs::quat_interpolate(&mut rd, &ra, &rb, t);
        c::oracle_quat_interpolate(&mut cd, &ca, &cb, t);
    }
    prop_assert!(same_q(rd, cd), "shim {rd:?} != C {cd:?}");
    let core: quat = Quat::from(ra).interpolate(rb.into(), t).into();
    prop_assert!(same_q(core, cd), "core {core:?} != C {cd:?}");
    Ok(())
}

fn check_tangent(p: [f32; 4], q: [f32; 4], n: [f32; 4]) -> Result<(), TestCaseError> {
    let (rp, cp) = pair(p);
    let (rq, cq) = pair(q);
    let (rn, cn) = pair(n);
    let mut rd = quat::default();
    let mut cd = OracleQuat::default();
    // SAFETY: all pointers refer to live locals.
    unsafe {
        rs::quat_get_tangent(&mut rd, &rp, &rq, &rn);
        c::oracle_quat_get_tangent(&mut cd, &cp, &cq, &cn);
    }
    prop_assert!(same_q(rd, cd), "shim {rd:?} != C {cd:?}");
    let core: quat = Quat::tangent(rp.into(), rq.into(), rn.into()).into();
    prop_assert!(same_q(core, cd), "core {core:?} != C {cd:?}");
    Ok(())
}

fn check_cubic(
    q1: [f32; 4],
    q2: [f32; 4],
    m1: [f32; 4],
    m2: [f32; 4],
    t: f32,
) -> Result<(), TestCaseError> {
    let (rq1, cq1) = pair(q1);
    let (rq2, cq2) = pair(q2);
    let (rm1, cm1) = pair(m1);
    let (rm2, cm2) = pair(m2);
    let mut rd = quat::default();
    let mut cd = OracleQuat::default();
    // SAFETY: all pointers refer to live locals.
    unsafe {
        rs::quat_interpolate_cubic(&mut rd, &rq1, &rq2, &rm1, &rm2, t);
        c::oracle_quat_interpolate_cubic(&mut cd, &cq1, &cq2, &cm1, &cm2, t);
    }
    prop_assert!(same_q(rd, cd), "shim {rd:?} != C {cd:?}");
    let core: quat =
        Quat::interpolate_cubic(rq1.into(), rq2.into(), rm1.into(), rm2.into(), t).into();
    prop_assert!(same_q(core, cd), "core {core:?} != C {cd:?}");
    Ok(())
}

fn any_bits() -> impl Strategy<Value = f32> {
    any::<u32>().prop_map(f32::from_bits)
}

fn quat_bits() -> impl Strategy<Value = [f32; 4]> {
    [any_bits(), any_bits(), any_bits(), any_bits()]
}

fn row() -> impl Strategy<Value = [f32; 4]> + Clone {
    [any::<u32>(), any::<u32>(), any::<u32>(), any::<u32>()]
        .prop_map(|bits| bits.map(f32::from_bits))
}

fn rows() -> impl Strategy<Value = [[f32; 4]; 4]> {
    [row(), row(), row(), row()]
}

proptest! {
    #![proptest_config(ProptestConfig::with_cases(1024))]

    #[test]
    fn mul_matches_c_oracle(a in quat_bits(), b in quat_bits()) {
        check_mul(a, b)?;
    }

    #[test]
    fn from_axisang_matches_c_oracle(a in quat_bits()) {
        check_axisang(a)?;
    }

    #[test]
    fn from_matrix4_matches_c_oracle(m in rows()) {
        check_m4(m)?;
    }

    #[test]
    fn from_matrix3_matches_c_oracle(m in rows()) {
        check_m3(m)?;
    }

    #[test]
    fn get_dir_matches_c_oracle(q in quat_bits()) {
        check_dir(q)?;
    }

    #[test]
    fn set_look_dir_matches_c_oracle(d in quat_bits()) {
        check_look(d)?;
    }

    #[test]
    fn log_matches_c_oracle(q in quat_bits()) {
        check_log(q)?;
    }

    #[test]
    fn exp_matches_c_oracle(q in quat_bits()) {
        check_exp(q)?;
    }

    #[test]
    fn interpolate_matches_c_oracle(a in quat_bits(), b in quat_bits(), t in any_bits()) {
        check_interp(a, b, t)?;
    }

    #[test]
    fn tangent_matches_c_oracle(p in quat_bits(), q in quat_bits(), n in quat_bits()) {
        check_tangent(p, q, n)?;
    }

    #[test]
    fn cubic_matches_c_oracle(
        q1 in quat_bits(),
        q2 in quat_bits(),
        m1 in quat_bits(),
        m2 in quat_bits(),
        t in any_bits(),
    ) {
        check_cubic(q1, q2, m1, m2, t)?;
    }
}

/// Every arrangement of overlapping pointers must give the non-aliased
/// result, on the shim and on the oracle alike.
fn assert_aliased(r: quat, c: OracleQuat, rref: quat, cref: OracleQuat, what: &str) {
    assert!(same_q(r, c), "{what}: shim {r:?} != C {c:?}");
    assert!(
        same(r.x, rref.x) && same(r.y, rref.y) && same(r.z, rref.z) && same(r.w, rref.w),
        "{what}: shim {r:?} != non-aliased {rref:?}"
    );
    assert!(
        same(c.x, cref.x) && same(c.y, cref.y) && same(c.z, cref.z) && same(c.w, cref.w),
        "{what}: C {c:?} != non-aliased {cref:?}"
    );
}

/// `dst` may alias an input, and all three pointers may name the same
/// object. The C and Rust results must both match the non-aliased product.
#[test]
fn mul_dst_aliases_input() {
    let (ra, ca) = pair([0.2, -0.3, 0.4, 0.8]);
    let (rb, cb) = pair([-0.5, 0.6, -0.7, 0.9]);
    let mut rr = quat::default();
    let mut cr = OracleQuat::default();
    // SAFETY: all pointers refer to live locals.
    unsafe {
        rs::quat_mul(&mut rr, &ra, &rb);
        c::oracle_quat_mul(&mut cr, &ca, &cb);
    }

    // Quaternion squared, the reference for the all-equal arrangement.
    let mut rr_sq = quat::default();
    let mut cr_sq = OracleQuat::default();
    // SAFETY: all pointers refer to live locals.
    unsafe {
        rs::quat_mul(&mut rr_sq, &ra, &ra);
        c::oracle_quat_mul(&mut cr_sq, &ca, &ca);
    }

    let mut r1 = ra;
    let mut c1 = ca;
    let r1_ptr: *mut quat = &mut r1;
    let c1_ptr: *mut OracleQuat = &mut c1;
    // SAFETY: dst may alias q1; the pointers name live locals.
    unsafe {
        rs::quat_mul(r1_ptr, r1_ptr, &rb);
        c::oracle_quat_mul(c1_ptr, c1_ptr, &cb);
    }
    assert_aliased(r1, c1, rr, cr, "dst == q1");

    let mut r2 = rb;
    let mut c2 = cb;
    let r2_ptr: *mut quat = &mut r2;
    let c2_ptr: *mut OracleQuat = &mut c2;
    // SAFETY: dst may alias q2; the pointers name live locals.
    unsafe {
        rs::quat_mul(r2_ptr, &ra, r2_ptr);
        c::oracle_quat_mul(c2_ptr, &ca, c2_ptr);
    }
    assert_aliased(r2, c2, rr, cr, "dst == q2");

    let mut r3 = ra;
    let mut c3 = ca;
    let r3_ptr: *mut quat = &mut r3;
    let c3_ptr: *mut OracleQuat = &mut c3;
    // SAFETY: all three pointers may name one object; it is live.
    unsafe {
        rs::quat_mul(r3_ptr, r3_ptr, r3_ptr);
        c::oracle_quat_mul(c3_ptr, c3_ptr, c3_ptr);
    }
    assert_aliased(r3, c3, rr_sq, cr_sq, "dst == q1 == q2");
}

/// A `quat` and a `matrix3` sharing storage. `quat_from_matrix3` reaches its
/// matrix through a `matrix4` cast, so an output lane can overwrite a matrix
/// element that the formula still has to read.
#[repr(C)]
union Matrix3Quat {
    m: matrix3,
    q: quat,
}

/// The oracle side of the same arrangement.
#[repr(C)]
union OracleMatrix3Quat {
    m: OracleMatrix3,
    q: OracleQuat,
}

/// A `quat` and a `matrix4` sharing storage, for the matrix4 entry point.
#[repr(C)]
union Matrix4Quat {
    m: matrix4,
    q: quat,
}

/// The oracle side of the same arrangement.
#[repr(C)]
union OracleMatrix4Quat {
    m: OracleMatrix4,
    q: OracleQuat,
}

#[test]
fn from_matrix3_same_address() {
    let rows = [
        [0.0, 1.0, 0.0, 0.0],
        [-1.0, 0.0, 0.0, 0.0],
        [0.0, 0.0, 1.0, 0.0],
        [0.0, 0.0, 0.0, 0.0],
    ];
    let (rm, cm) = m3_pair(rows);
    let mut rr = quat::default();
    let mut cr = OracleQuat::default();
    // SAFETY: all pointers refer to live locals.
    unsafe {
        rs::quat_from_matrix3(&mut rr, &rm);
        c::oracle_quat_from_matrix3(&mut cr, &cm);
    }

    let mut shared = Matrix3Quat { m: rm };
    let mut shared_c = OracleMatrix3Quat { m: cm };
    // SAFETY: the destination quaternion and the source matrix share storage,
    // which both implementations must tolerate.
    unsafe {
        rs::quat_from_matrix3(&mut shared.q, &shared.m);
        c::oracle_quat_from_matrix3(&mut shared_c.q, &shared_c.m);
    }
    // SAFETY: each union holds the written quaternion, read whole.
    let (r, c) = unsafe { (shared.q, shared_c.q) };
    assert_aliased(r, c, rr, cr, "matrix3 same address");
}

#[test]
fn from_matrix4_same_address() {
    // Positive trace: the `tr > 0` branch reads m->x.y after writing dst->y.
    let rows = [
        [0.0, 1.0, 0.0, 0.0],
        [-1.0, 0.0, 0.0, 0.0],
        [0.0, 0.0, 1.0, 0.0],
        [0.0, 0.0, 0.0, 1.0],
    ];
    let (rm, cm) = m4_pair(rows);
    let mut rr = quat::default();
    let mut cr = OracleQuat::default();
    // SAFETY: all pointers refer to live locals.
    unsafe {
        rs::quat_from_matrix4(&mut rr, &rm);
        c::oracle_quat_from_matrix4(&mut cr, &cm);
    }

    let mut shared = Matrix4Quat { m: rm };
    let mut shared_c = OracleMatrix4Quat { m: cm };
    // SAFETY: the destination quaternion and the source matrix share storage,
    // which both implementations must tolerate.
    unsafe {
        rs::quat_from_matrix4(&mut shared.q, &shared.m);
        c::oracle_quat_from_matrix4(&mut shared_c.q, &shared_c.m);
    }
    // SAFETY: each union holds the written quaternion, read whole.
    let (r, c) = unsafe { (shared.q, shared_c.q) };
    assert_aliased(r, c, rr, cr, "matrix4 same address");
}

#[test]
fn from_matrix4_same_address_negative_trace() {
    // 180 degrees about Y: the negative branch picks diagonal index 1, where
    // dst->ptr[0] reads the lanes dst->ptr[1] just overwrote.
    let rows = [
        [-1.0, 0.0, 0.0, 0.0],
        [0.0, 1.0, 0.0, 0.0],
        [0.0, 0.0, -1.0, 0.0],
        [0.0, 0.0, 0.0, 1.0],
    ];
    let (rm, cm) = m4_pair(rows);
    let mut rr = quat::default();
    let mut cr = OracleQuat::default();
    // SAFETY: all pointers refer to live locals.
    unsafe {
        rs::quat_from_matrix4(&mut rr, &rm);
        c::oracle_quat_from_matrix4(&mut cr, &cm);
    }

    let mut shared = Matrix4Quat { m: rm };
    let mut shared_c = OracleMatrix4Quat { m: cm };
    // SAFETY: the destination quaternion and the source matrix share storage,
    // which both implementations must tolerate.
    unsafe {
        rs::quat_from_matrix4(&mut shared.q, &shared.m);
        c::oracle_quat_from_matrix4(&mut shared_c.q, &shared_c.m);
    }
    // SAFETY: each union holds the written quaternion, read whole.
    let (r, c) = unsafe { (shared.q, shared_c.q) };
    assert_aliased(r, c, rr, cr, "matrix4 negative trace same address");
}

/// The cmocka cases, checked bit-for-bit against the oracle where the C
/// result is exact, and within the test's tolerance otherwise.
#[test]
fn cmocka_cases_match_c_oracle() {
    check_axisang([0.0, 1.0, 0.0, 0.0]).unwrap();
    check_mul([0.0, 0.0, 0.0, 1.0], [0.0, 0.0, 0.0, 1.0]).unwrap();
    check_m4([
        [1.0, 0.0, 0.0, 0.0],
        [0.0, 1.0, 0.0, 0.0],
        [0.0, 0.0, 1.0, 0.0],
        [0.0, 0.0, 0.0, 1.0],
    ])
    .unwrap();
    check_m4([
        [1.0, 0.0, 0.0, 0.0],
        [0.0, -1.0, 0.0, 0.0],
        [0.0, 0.0, -1.0, 0.0],
        [0.0, 0.0, 0.0, 1.0],
    ])
    .unwrap();
    check_m3([
        [1.0, 0.0, 0.0, 0.0],
        [0.0, 1.0, 0.0, 0.0],
        [0.0, 0.0, 1.0, 0.0],
        [0.0, 0.0, 0.0, 0.0],
    ])
    .unwrap();
    check_dir([0.0, 0.0, 0.0, 1.0]).unwrap();
    check_look([0.0, 0.0, -1.0, 0.0]).unwrap();
    check_log([0.0, 0.0, 0.0, 1.0]).unwrap();
    check_exp([0.0, 0.0, 0.0, 0.0]).unwrap();
    check_interp([0.1, -0.2, 0.3, 0.4], [0.1, -0.2, 0.3, 0.4], 0.0).unwrap();
    check_tangent(
        [0.2, 0.3, -0.4, 0.5],
        [0.2, 0.3, -0.4, 0.5],
        [0.2, 0.3, -0.4, 0.5],
    )
    .unwrap();
    check_cubic(
        [0.0, 0.0, 0.0, 1.0],
        [1.0, 0.0, 0.0, 0.0],
        [0.0, 1.0, 0.0, 0.0],
        [0.0, 0.0, 1.0, 0.0],
        0.0,
    )
    .unwrap();

    // The values the cmocka test treats as exact.
    let id = Quat::from_axisang(AxisAng::new(0.0, 1.0, 0.0, 0.0));
    assert_eq!([id.x, id.y, id.z, id.w], [0.0, 0.0, 0.0, 1.0]);
}
