//! Tier 3: the Rust C ABI shims and safe core behave exactly like the
//! original C, compiled as an oracle.
//!
//! No intentional differences from C. Inputs set the `w` lane too: the C
//! code multiplies it in `vec3_dot`, so a nonzero or NaN `w` from a caller
//! must change the result the same way.
//!
//! Reminder: the mutation check (break the core, see this file fail) is done
//! once per port, not on every change.

use obs_c_oracle::graphics_math::{
    self as c, OracleMatrix3, OracleMatrix4, OraclePlane, OracleVec3, OracleVec4,
};
use obs_graphics::ffi::matrix3::matrix3;
use obs_graphics::ffi::matrix4::matrix4;
use obs_graphics::ffi::plane::plane;
use obs_graphics::ffi::vec3::{self as rs, vec3};
use obs_graphics::ffi::vec4::vec4;
use obs_graphics::vec3::Vec3;
use proptest::prelude::*;

/// Bit-exact equality, except that any NaN equals any NaN.
fn same(a: f32, b: f32) -> bool {
    a.to_bits() == b.to_bits() || (a.is_nan() && b.is_nan())
}

fn same3(r: vec3, c: OracleVec3) -> bool {
    same(r.x, c.x) && same(r.y, c.y) && same(r.z, c.z) && same(r.w, c.w)
}

type Lanes = [f32; 4];

/// Any bit pattern, so NaNs, infinities, zeros and subnormals all appear.
fn any_bits() -> BoxedStrategy<f32> {
    any::<u32>().prop_map(f32::from_bits).boxed()
}

/// Finite values of moderate size, where the summation order decides the
/// last bits.
fn finite() -> BoxedStrategy<f32> {
    prop_oneof![
        -1.0e6_f32..1.0e6,
        -1.0_f32..1.0,
        (-8i32..8).prop_map(|n| n as f32),
    ]
    .boxed()
}

/// A vec3 as C callers build it: `w` is 0 most of the time, but any value
/// some of the time.
fn arb_vec3(f: fn() -> BoxedStrategy<f32>) -> impl Strategy<Value = Lanes> {
    (f(), f(), f(), prop_oneof![3 => Just(0.0_f32), 1 => f()]).prop_map(|(x, y, z, w)| [x, y, z, w])
}

fn arb_vec4(f: fn() -> BoxedStrategy<f32>) -> impl Strategy<Value = Lanes> {
    [f(), f(), f(), f()]
}

fn arb_rows(f: fn() -> BoxedStrategy<f32>) -> impl Strategy<Value = [Lanes; 4]> {
    [arb_vec3(f), arb_vec3(f), arb_vec3(f), arb_vec3(f)]
}

fn arb_rows4(f: fn() -> BoxedStrategy<f32>) -> impl Strategy<Value = [Lanes; 4]> {
    [arb_vec4(f), arb_vec4(f), arb_vec4(f), arb_vec4(f)]
}

fn r3(a: Lanes) -> vec3 {
    vec3 {
        x: a[0],
        y: a[1],
        z: a[2],
        w: a[3],
    }
}

fn c3(a: Lanes) -> OracleVec3 {
    OracleVec3 {
        x: a[0],
        y: a[1],
        z: a[2],
        w: a[3],
    }
}

fn r4(a: Lanes) -> vec4 {
    vec4 {
        x: a[0],
        y: a[1],
        z: a[2],
        w: a[3],
    }
}

fn c4(a: Lanes) -> OracleVec4 {
    OracleVec4 {
        x: a[0],
        y: a[1],
        z: a[2],
        w: a[3],
    }
}

fn rm3(m: [Lanes; 4]) -> matrix3 {
    matrix3 {
        x: r3(m[0]),
        y: r3(m[1]),
        z: r3(m[2]),
        t: r3(m[3]),
    }
}

fn cm3(m: [Lanes; 4]) -> OracleMatrix3 {
    OracleMatrix3 {
        x: c3(m[0]),
        y: c3(m[1]),
        z: c3(m[2]),
        t: c3(m[3]),
    }
}

fn rm4(m: [Lanes; 4]) -> matrix4 {
    matrix4 {
        x: r4(m[0]),
        y: r4(m[1]),
        z: r4(m[2]),
        t: r4(m[3]),
    }
}

fn cm4(m: [Lanes; 4]) -> OracleMatrix4 {
    OracleMatrix4 {
        x: c4(m[0]),
        y: c4(m[1]),
        z: c4(m[2]),
        t: c4(m[3]),
    }
}

/// A shim/oracle pair of `fn(dst, v, extra)` that both accept `dst == v`.
type RsOp<E> = unsafe extern "C" fn(*mut vec3, *const vec3, *const E);
type COp<E> = unsafe extern "C" fn(*mut OracleVec3, *const OracleVec3, *const E);

/// Runs one operation out of place and in place on both sides, and checks
/// the safe core's answer against C.
fn check_op<RE, CE>(
    v: Lanes,
    (rs_op, rs_extra): (RsOp<RE>, &RE),
    (c_op, c_extra): (COp<CE>, &CE),
    core: Vec3,
) -> Result<(), TestCaseError> {
    let rv = r3(v);
    let cv = c3(v);
    let mut rd = vec3::default();
    let mut cd = OracleVec3::default();
    // SAFETY: all pointers refer to live, properly aligned locals.
    unsafe {
        rs_op(&mut rd, &rv, rs_extra);
        c_op(&mut cd, &cv, c_extra);
    }
    prop_assert!(same3(rd, cd), "shim {rd:?} != C {cd:?}");
    let core: vec3 = core.into();
    prop_assert!(same3(core, cd), "core {core:?} != C {cd:?}");

    let mut ra = rv;
    let mut ca = cv;
    let ra_ptr: *mut vec3 = &mut ra;
    let ca_ptr: *mut OracleVec3 = &mut ca;
    // SAFETY: the pointers refer to live, aligned locals; aliasing dst and
    // v is allowed by both implementations.
    unsafe {
        rs_op(ra_ptr, ra_ptr, rs_extra);
        c_op(ca_ptr, ca_ptr, c_extra);
    }
    prop_assert!(same3(ra, ca), "in place {ra:?} != C {ca:?}");
    Ok(())
}

fn check_all(
    v: Lanes,
    other: Lanes,
    dist: f32,
    rows3: [Lanes; 4],
    rows4: [Lanes; 4],
) -> Result<(), TestCaseError> {
    let core_v = Vec3::from(r3(v));
    let (rp, cp) = (
        plane {
            dir: r3(other),
            dist,
        },
        OraclePlane {
            dir: c3(other),
            dist,
        },
    );
    let (rm3, cm3) = (rm3(rows3), cm3(rows3));
    let (rm4, cm4) = (rm4(rows4), cm4(rows4));

    // SAFETY: all pointers refer to live, properly aligned locals.
    let (rd, cd) = unsafe {
        (
            rs::vec3_plane_dist(&r3(v), &rp),
            c::oracle_vec3_plane_dist(&c3(v), &cp),
        )
    };
    prop_assert!(same(rd, cd), "plane_dist {rd} != C {cd}");
    prop_assert!(same(core_v.plane_dist(&rp.into()), cd));

    check_op(
        v,
        (rs::vec3_rotate, &rm3),
        (c::oracle_vec3_rotate, &cm3),
        core_v.rotate(&rm3.into()),
    )?;
    check_op(
        v,
        (rs::vec3_transform3x4, &rm3),
        (c::oracle_vec3_transform3x4, &cm3),
        core_v.transform3x4(&rm3.into()),
    )?;
    check_op(
        v,
        (rs::vec3_transform, &rm4),
        (c::oracle_vec3_transform, &cm4),
        core_v.transform(&rm4.into()),
    )?;
    check_op(
        v,
        (rs::vec3_mirror, &rp),
        (c::oracle_vec3_mirror, &cp),
        core_v.mirror(&rp.into()),
    )?;
    check_op(
        v,
        (rs::vec3_mirrorv, &r3(other)),
        (c::oracle_vec3_mirrorv, &c3(other)),
        core_v.mirrorv(r3(other).into()),
    )?;

    // vec3_mirrorv with dst aliasing the direction instead of v.
    let mut ra = r3(other);
    let mut ca = c3(other);
    let ra_ptr: *mut vec3 = &mut ra;
    let ca_ptr: *mut OracleVec3 = &mut ca;
    // SAFETY: live, aligned locals; dst may alias either input.
    unsafe {
        rs::vec3_mirrorv(ra_ptr, &r3(v), ra_ptr);
        c::oracle_vec3_mirrorv(ca_ptr, &c3(v), ca_ptr);
    }
    prop_assert!(same3(ra, ca), "mirrorv dst == vec {ra:?} != C {ca:?}");
    Ok(())
}

proptest! {
    #[test]
    fn from_vec4_matches_c_oracle(v in arb_vec4(any_bits)) {
        let mut rd = vec3::default();
        let mut cd = OracleVec3::default();
        // SAFETY: all pointers refer to live, properly aligned locals.
        unsafe {
            rs::vec3_from_vec4(&mut rd, &r4(v));
            c::oracle_vec3_from_vec4(&mut cd, &c4(v));
        }
        prop_assert!(same3(rd, cd), "shim {rd:?} != C {cd:?}");
    }

    #[test]
    fn ops_match_c_oracle_any_bits(
        v in arb_vec3(any_bits),
        other in arb_vec3(any_bits),
        dist in any_bits(),
        rows3 in arb_rows(any_bits),
        rows4 in arb_rows4(any_bits),
    ) {
        check_all(v, other, dist, rows3, rows4)?;
    }

    #[test]
    fn ops_match_c_oracle_finite(
        v in arb_vec3(finite),
        other in arb_vec3(finite),
        dist in finite(),
        rows3 in arb_rows(finite),
        rows4 in arb_rows4(finite),
    ) {
        check_all(v, other, dist, rows3, rows4)?;
    }
}

/// `vec3_rand` draws from the C library's `rand` through libobs
/// `rand_float`. Seed the same sequence for each side and compare. The
/// whole check runs in one test so no other thread draws in between.
#[test]
fn rand_matches_c_oracle() {
    for seed in [0u32, 1, 42, 0xdead_beef] {
        for positive_only in [0, 1, 7] {
            for _ in 0..3 {
                let mut rd = vec3::default();
                let mut cd = OracleVec3::default();
                // SAFETY: srand and rand only touch the C library's state;
                // the pointers refer to live, aligned locals.
                unsafe {
                    libc::srand(seed);
                    rs::vec3_rand(&mut rd, positive_only);
                    libc::srand(seed);
                    c::oracle_vec3_rand(&mut cd, positive_only);
                }
                assert!(same3(rd, cd), "seed {seed}: {rd:?} != C {cd:?}");
                assert_eq!(rd.w, 0.0);
                if positive_only != 0 {
                    assert!([rd.x, rd.y, rd.z].iter().all(|f| (0.0..=1.0).contains(f)));
                } else {
                    assert!([rd.x, rd.y, rd.z].iter().all(|f| (-1.0..=1.0).contains(f)));
                }
            }
        }
    }
}

/// The cases from `test/cmocka/test_vec3.c`, run against the oracle so the
/// C test's expectations are checked on every platform.
#[test]
fn cmocka_cases_match_c_oracle() {
    let axes = [
        [0.0, 1.0, 0.0, 0.0],
        [0.0, 0.0, 1.0, 0.0],
        [1.0, 0.0, 0.0, 0.0],
        [10.0, 20.0, 30.0, 0.0],
    ];
    let cm = cm3(axes);
    let floor = OraclePlane {
        dir: c3([0.0, 1.0, 0.0, 0.0]),
        dist: 2.0,
    };
    let v = c3([1.0, 2.0, 3.0, 0.0]);
    let mut d = OracleVec3::default();
    // SAFETY: all pointers refer to live, properly aligned locals.
    unsafe {
        assert_eq!(
            c::oracle_vec3_plane_dist(&c3([4.0, 5.0, 6.0, 0.0]), &floor),
            3.0
        );
        c::oracle_vec3_rotate(&mut d, &v, &cm);
        assert_eq!([d.x, d.y, d.z, d.w], [2.0, 3.0, 1.0, 0.0]);
        c::oracle_vec3_transform3x4(&mut d, &v, &cm);
        assert_eq!([d.x, d.y, d.z, d.w], [-18.0, -27.0, -9.0, 0.0]);
        c::oracle_vec3_mirror(&mut d, &c3([4.0, 5.0, 6.0, 0.0]), &floor);
        assert_eq!([d.x, d.y, d.z, d.w], [4.0, -1.0, 6.0, 0.0]);
        c::oracle_vec3_mirrorv(&mut d, &v, &c3([1.0, 0.0, 0.0, 0.0]));
        assert_eq!([d.x, d.y, d.z, d.w], [-1.0, 2.0, 3.0, 0.0]);
        c::oracle_vec3_from_vec4(&mut d, &c4([1.0, 2.0, 3.0, 4.0]));
        assert_eq!([d.x, d.y, d.z, d.w], [1.0, 2.0, 3.0, 0.0]);
    }
    let translate = [
        [1.0, 0.0, 0.0, 0.0],
        [0.0, 1.0, 0.0, 0.0],
        [0.0, 0.0, 1.0, 0.0],
        [10.0, 20.0, 30.0, 1.0],
    ];
    // SAFETY: as above.
    unsafe { c::oracle_vec3_transform(&mut d, &v, &cm4(translate)) };
    assert_eq!([d.x, d.y, d.z, d.w], [11.0, 22.0, 33.0, 0.0]);
    check_all(
        [1.0, 2.0, 3.0, 0.0],
        [0.0, 1.0, 0.0, 0.0],
        2.0,
        axes,
        translate,
    )
    .unwrap();
}
