//! Tier 3: the Rust C ABI shims and safe core behave exactly like the
//! original C, compiled as an oracle.
//!
//! No intentional differences from C.
//!
//! Reminder: the mutation check (break the core, see this file fail) is done
//! once per port, not on every change.

use obs_c_oracle::graphics_math::{self as c, OracleMatrix4, OracleVec3, OracleVec4};
use obs_graphics::ffi::matrix4::matrix4;
use obs_graphics::ffi::vec3::vec3;
use obs_graphics::ffi::vec4::{self as rs, vec4};
use obs_graphics::matrix4::Matrix4;
use obs_graphics::vec4::Vec4;
use proptest::prelude::*;

/// Bit-exact equality, except that any NaN equals any NaN.
fn same(a: f32, b: f32) -> bool {
    a.to_bits() == b.to_bits() || (a.is_nan() && b.is_nan())
}

fn same4(r: vec4, c: OracleVec4) -> bool {
    same(r.x, c.x) && same(r.y, c.y) && same(r.z, c.z) && same(r.w, c.w)
}

/// Any bit pattern, so NaNs, infinities, zeros and subnormals all appear.
fn arb_f32() -> impl Strategy<Value = f32> {
    any::<u32>().prop_map(f32::from_bits)
}

/// Finite values of moderate size, where products and sums stay finite
/// and the summation order decides the last bits.
fn finite_f32() -> impl Strategy<Value = f32> {
    prop_oneof![
        -1.0e6_f32..1.0e6,
        -1.0_f32..1.0,
        (-8i32..8).prop_map(|n| n as f32),
    ]
}

fn arb_vec(f: fn() -> BoxedStrategy<f32>) -> impl Strategy<Value = [f32; 4]> {
    [f(), f(), f(), f()]
}

fn any_bits() -> BoxedStrategy<f32> {
    arb_f32().boxed()
}

fn finite() -> BoxedStrategy<f32> {
    finite_f32().boxed()
}

fn rs_vec4(a: [f32; 4]) -> vec4 {
    vec4 {
        x: a[0],
        y: a[1],
        z: a[2],
        w: a[3],
    }
}

fn c_vec4(a: [f32; 4]) -> OracleVec4 {
    OracleVec4 {
        x: a[0],
        y: a[1],
        z: a[2],
        w: a[3],
    }
}

fn rs_matrix(rows: [[f32; 4]; 4]) -> matrix4 {
    matrix4 {
        x: rs_vec4(rows[0]),
        y: rs_vec4(rows[1]),
        z: rs_vec4(rows[2]),
        t: rs_vec4(rows[3]),
    }
}

fn c_matrix(rows: [[f32; 4]; 4]) -> OracleMatrix4 {
    OracleMatrix4 {
        x: c_vec4(rows[0]),
        y: c_vec4(rows[1]),
        z: c_vec4(rows[2]),
        t: c_vec4(rows[3]),
    }
}

fn check_transform(v: [f32; 4], rows: [[f32; 4]; 4]) -> Result<(), TestCaseError> {
    let rv = rs_vec4(v);
    let cv = c_vec4(v);
    let rm = rs_matrix(rows);
    let cm = c_matrix(rows);
    let mut rd = vec4::default();
    let mut cd = OracleVec4::default();

    // SAFETY: all pointers refer to live, properly aligned locals.
    unsafe {
        rs::vec4_transform(&mut rd, &rv, &rm);
        c::oracle_vec4_transform(&mut cd, &cv, &cm);
    }
    prop_assert!(same4(rd, cd), "shim {rd:?} != C {cd:?}");

    let core: vec4 = Vec4::from(rv).transform(&Matrix4::from(rm)).into();
    prop_assert!(same4(core, cd), "core {core:?} != C {cd:?}");

    // dst aliasing the source, on both sides.
    let mut ra = rv;
    let mut ca = cv;
    let ra_ptr: *mut vec4 = &mut ra;
    let ca_ptr: *mut OracleVec4 = &mut ca;
    // SAFETY: the pointers refer to live, aligned locals; aliasing dst and
    // v is allowed by both implementations.
    unsafe {
        rs::vec4_transform(ra_ptr, ra_ptr, &rm);
        c::oracle_vec4_transform(ca_ptr, ca_ptr, &cm);
    }
    prop_assert!(same4(ra, ca), "in place {ra:?} != C {ca:?}");
    Ok(())
}

proptest! {
    #[test]
    fn from_vec3_matches_c_oracle(v in arb_vec(any_bits)) {
        let rv = vec3 { x: v[0], y: v[1], z: v[2], w: v[3] };
        let cv = OracleVec3 { x: v[0], y: v[1], z: v[2], w: v[3] };
        let mut rd = vec4::default();
        let mut cd = OracleVec4::default();

        // SAFETY: all pointers refer to live, properly aligned locals.
        unsafe {
            rs::vec4_from_vec3(&mut rd, &rv);
            c::oracle_vec4_from_vec3(&mut cd, &cv);
        }
        prop_assert!(same4(rd, cd), "shim {rd:?} != C {cd:?}");
        let core: vec4 = Vec4::from_xyz(v[0], v[1], v[2]).into();
        prop_assert!(same4(core, cd), "core {core:?} != C {cd:?}");
    }

    #[test]
    fn transform_matches_c_oracle_any_bits(
        v in arb_vec(any_bits),
        rows in [arb_vec(any_bits), arb_vec(any_bits), arb_vec(any_bits), arb_vec(any_bits)],
    ) {
        check_transform(v, rows)?;
    }

    #[test]
    fn transform_matches_c_oracle_finite(
        v in arb_vec(finite),
        rows in [arb_vec(finite), arb_vec(finite), arb_vec(finite), arb_vec(finite)],
    ) {
        check_transform(v, rows)?;
    }
}

/// The cases from `test/cmocka/test_vec4.c`, run against the oracle so the
/// C test's expectations are checked on every platform.
#[test]
fn cmocka_cases_match_c_oracle() {
    let counting = [
        [1.0, 2.0, 3.0, 4.0],
        [5.0, 6.0, 7.0, 8.0],
        [9.0, 10.0, 11.0, 12.0],
        [13.0, 14.0, 15.0, 16.0],
    ];
    let translate = [
        [1.0, 0.0, 0.0, 0.0],
        [0.0, 1.0, 0.0, 0.0],
        [0.0, 0.0, 1.0, 0.0],
        [10.0, 20.0, 30.0, 1.0],
    ];
    let cases = [
        ([1.0, 0.0, 0.0, 0.0], counting, [1.0, 2.0, 3.0, 4.0]),
        ([0.0, 0.0, 0.0, 1.0], counting, [13.0, 14.0, 15.0, 16.0]),
        ([1.0, 1.0, 1.0, 1.0], counting, [28.0, 32.0, 36.0, 40.0]),
        ([1.0, -1.0, 2.0, 0.5], counting, [20.5, 23.0, 25.5, 28.0]),
        ([1.0, 2.0, 3.0, 1.0], translate, [11.0, 22.0, 33.0, 1.0]),
        ([1.0, 2.0, 3.0, 0.0], translate, [1.0, 2.0, 3.0, 0.0]),
    ];
    for (v, rows, want) in cases {
        let mut cd = OracleVec4::default();
        // SAFETY: all pointers refer to live, properly aligned locals.
        unsafe { c::oracle_vec4_transform(&mut cd, &c_vec4(v), &c_matrix(rows)) };
        assert_eq!([cd.x, cd.y, cd.z, cd.w], want, "C oracle for {v:?}");
        check_transform(v, rows).unwrap();
    }
}
