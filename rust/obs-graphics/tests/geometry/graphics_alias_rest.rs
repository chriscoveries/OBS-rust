//! The overlap arrangements left after part 1 of #370.
//!
//! Each case runs the call on separate storage and on the overlap, and
//! requires the same bits from the C oracle and from the Rust shim.
//! `get_3x3_submatrix` is private to `matrix4.c`. `gs_u8x4_to_float4` is a
//! header inline, covered by `test_graphics_alias_rest`.

use obs_c_oracle::graphics_math::{
    self as c, OracleMatrix3, OracleMatrix4, OraclePlane, OracleQuat, OracleVec3, OracleVec4,
};
use obs_c_oracle::vec2::OracleVec2;
use obs_graphics::ffi::math_extra::{self as mx};
use obs_graphics::ffi::matrix3::{self as m3, matrix3};
use obs_graphics::ffi::matrix4::matrix4;
use obs_graphics::ffi::quat::{self as qt, quat};
use obs_graphics::ffi::vec2::vec2;
use obs_graphics::ffi::vec3::{self as v3, vec3};
use obs_graphics::ffi::vec4::vec4;

fn same(a: f32, b: f32) -> bool {
    a.to_bits() == b.to_bits()
}

fn same_v(a: vec3, b: vec3) -> bool {
    same(a.x, b.x) && same(a.y, b.y) && same(a.z, b.z) && same(a.w, b.w)
}

fn same_o(a: OracleVec3, b: OracleVec3) -> bool {
    same(a.x, b.x) && same(a.y, b.y) && same(a.z, b.z) && same(a.w, b.w)
}

fn v(x: f32, y: f32, z: f32) -> vec3 {
    vec3 { x, y, z, w: 0.0 }
}

fn ov(x: f32, y: f32, z: f32) -> OracleVec3 {
    OracleVec3 { x, y, z, w: 0.0 }
}

fn v4(x: f32, y: f32, z: f32, w: f32) -> vec4 {
    vec4 { x, y, z, w }
}

fn ov4(x: f32, y: f32, z: f32, w: f32) -> OracleVec4 {
    OracleVec4 { x, y, z, w }
}

fn ident3() -> matrix3 {
    matrix3 {
        x: v(1.0, 0.0, 0.0),
        y: v(0.0, 1.0, 0.0),
        z: v(0.0, 0.0, 1.0),
        t: v(0.0, 0.0, 0.0),
    }
}

fn oident3() -> OracleMatrix3 {
    OracleMatrix3 {
        x: ov(1.0, 0.0, 0.0),
        y: ov(0.0, 1.0, 0.0),
        z: ov(0.0, 0.0, 1.0),
        t: ov(0.0, 0.0, 0.0),
    }
}

fn rot_z4() -> matrix4 {
    matrix4 {
        x: v4(0.0, -1.0, 0.0, 0.0),
        y: v4(1.0, 0.0, 0.0, 0.0),
        z: v4(0.0, 0.0, 1.0, 0.0),
        t: v4(0.0, 0.0, 0.0, 1.0),
    }
}

fn orot_z4() -> OracleMatrix4 {
    OracleMatrix4 {
        x: ov4(0.0, -1.0, 0.0, 0.0),
        y: ov4(1.0, 0.0, 0.0, 0.0),
        z: ov4(0.0, 0.0, 1.0, 0.0),
        t: ov4(0.0, 0.0, 0.0, 1.0),
    }
}

fn rot_z3() -> matrix3 {
    matrix3 {
        x: v(0.0, -1.0, 0.0),
        y: v(1.0, 0.0, 0.0),
        z: v(0.0, 0.0, 1.0),
        t: v(0.0, 0.0, 0.0),
    }
}

fn orot_z3() -> OracleMatrix3 {
    OracleMatrix3 {
        x: ov(0.0, -1.0, 0.0),
        y: ov(1.0, 0.0, 0.0),
        z: ov(0.0, 0.0, 1.0),
        t: ov(0.0, 0.0, 0.0),
    }
}

#[test]
fn quat_mul_dst_aliases_q1() {
    let q = quat {
        x: 0.2,
        y: -0.3,
        z: 0.4,
        w: 0.8,
    };
    let cq = OracleQuat {
        x: 0.2,
        y: -0.3,
        z: 0.4,
        w: 0.8,
    };
    let mut separate = q;
    let mut alias = q;
    let mut c_separate = cq;
    let mut c_alias = cq;
    // SAFETY: each pointer refers to a live local. The alias call passes
    // that local as both dst and q1.
    unsafe {
        qt::quat_mul(&mut separate, &q, &q);
        qt::quat_mul(&mut alias, &alias, &q);
        c::oracle_quat_mul(&mut c_separate, &cq, &cq);
        c::oracle_quat_mul(&mut c_alias, &c_alias, &cq);
    }
    assert!(
        same(alias.x, separate.x)
            && same(alias.y, separate.y)
            && same(alias.z, separate.z)
            && same(alias.w, separate.w),
        "shim alias {alias:?} != separate {separate:?}"
    );
    assert!(
        same(c_alias.x, c_separate.x)
            && same(c_alias.y, c_separate.y)
            && same(c_alias.z, c_separate.z)
            && same(c_alias.w, c_separate.w),
        "C alias {c_alias:?} != separate {c_separate:?}"
    );
}

#[test]
fn quat_from_matrix4_same_address() {
    let src = rot_z4();
    let csrc = orot_z4();
    let mut separate = quat::default();
    let mut over = src;
    // SAFETY: the quaternion is the first 16 bytes of `over`.
    unsafe {
        qt::quat_from_matrix4(&mut separate, &src);
        let base = &mut over as *mut matrix4;
        qt::quat_from_matrix4(base as *mut quat, base);
    }
    let got = unsafe { *(&over as *const matrix4 as *const quat) };
    assert!(
        same(got.x, separate.x)
            && same(got.y, separate.y)
            && same(got.z, separate.z)
            && same(got.w, separate.w),
        "shim quat {got:?} != separate {separate:?}"
    );

    let mut c_separate = OracleQuat::default();
    let mut c_over = csrc;
    unsafe {
        c::oracle_quat_from_matrix4(&mut c_separate, &csrc);
        let base = &mut c_over as *mut OracleMatrix4;
        c::oracle_quat_from_matrix4(base as *mut OracleQuat, base);
    }
    let cgot = unsafe { *(&c_over as *const OracleMatrix4 as *const OracleQuat) };
    assert!(
        same(cgot.x, c_separate.x)
            && same(cgot.y, c_separate.y)
            && same(cgot.z, c_separate.z)
            && same(cgot.w, c_separate.w),
        "C quat {cgot:?} != separate {c_separate:?}"
    );
}

#[test]
fn quat_from_matrix3_same_address() {
    let src = rot_z3();
    let csrc = orot_z3();
    let mut separate = quat::default();
    let mut over = src;
    unsafe {
        qt::quat_from_matrix3(&mut separate, &src);
        let base = &mut over as *mut matrix3;
        qt::quat_from_matrix3(base as *mut quat, base);
    }
    let got = unsafe { *(&over as *const matrix3 as *const quat) };
    assert!(
        same(got.x, separate.x)
            && same(got.y, separate.y)
            && same(got.z, separate.z)
            && same(got.w, separate.w),
        "shim quat3 {got:?} != separate {separate:?}"
    );

    let mut c_separate = OracleQuat::default();
    let mut c_over = csrc;
    unsafe {
        c::oracle_quat_from_matrix3(&mut c_separate, &csrc);
        let base = &mut c_over as *mut OracleMatrix3;
        c::oracle_quat_from_matrix3(base as *mut OracleQuat, base);
    }
    let cgot = unsafe { *(&c_over as *const OracleMatrix3 as *const OracleQuat) };
    assert!(
        same(cgot.x, c_separate.x)
            && same(cgot.y, c_separate.y)
            && same(cgot.z, c_separate.z)
            && same(cgot.w, c_separate.w),
        "C quat3 {cgot:?} != separate {c_separate:?}"
    );
}

#[test]
fn vec3_rotate_dst_aliases_y() {
    let src = ident3();
    let csrc = oident3();
    let vel = v(1.0, 2.0, 3.0);
    let cvel = ov(1.0, 2.0, 3.0);
    let mut separate = vec3::default();
    let mut over = src;
    unsafe {
        v3::vec3_rotate(&mut separate, &vel, &src);
        let base = &mut over as *mut matrix3;
        v3::vec3_rotate(&mut (*base).y, &vel, base);
    }
    assert!(
        same_v(over.y, separate),
        "shim rotate {:?} != {separate:?}",
        over.y
    );

    let mut c_separate = OracleVec3::default();
    let mut c_over = csrc;
    unsafe {
        c::oracle_vec3_rotate(&mut c_separate, &cvel, &csrc);
        let base = &mut c_over as *mut OracleMatrix3;
        c::oracle_vec3_rotate(&mut (*base).y, &cvel, base);
    }
    assert!(
        same_o(c_over.y, c_separate),
        "C rotate {:?} != {c_separate:?}",
        c_over.y
    );
}

#[test]
fn vec3_transform3x4_dst_aliases_y() {
    let src = ident3();
    let csrc = oident3();
    let vel = v(1.0, 2.0, 3.0);
    let cvel = ov(1.0, 2.0, 3.0);
    let mut separate = vec3::default();
    let mut over = src;
    unsafe {
        v3::vec3_transform3x4(&mut separate, &vel, &src);
        let base = &mut over as *mut matrix3;
        v3::vec3_transform3x4(&mut (*base).y, &vel, base);
    }
    assert!(
        same_v(over.y, separate),
        "shim transform {:?} != {separate:?}",
        over.y
    );

    let mut c_separate = OracleVec3::default();
    let mut c_over = csrc;
    unsafe {
        c::oracle_vec3_transform3x4(&mut c_separate, &cvel, &csrc);
        let base = &mut c_over as *mut OracleMatrix3;
        c::oracle_vec3_transform3x4(&mut (*base).y, &cvel, base);
    }
    assert!(
        same_o(c_over.y, c_separate),
        "C transform {:?} != {c_separate:?}",
        c_over.y
    );
}

#[test]
fn matrix3_scale_v_aliases_x() {
    let mut src = ident3();
    src.x = v(2.0, 3.0, 4.0);
    src.y = v(1.0, 0.0, 0.0);
    src.z = v(0.0, 1.0, 0.0);
    src.t = v(5.0, 6.0, 7.0);
    let mut csrc = oident3();
    csrc.x = ov(2.0, 3.0, 4.0);
    csrc.y = ov(1.0, 0.0, 0.0);
    csrc.z = ov(0.0, 1.0, 0.0);
    csrc.t = ov(5.0, 6.0, 7.0);

    let mut separate = matrix3::default();
    let mut over = src;
    unsafe {
        m3::matrix3_scale(&mut separate, &src, &src.x);
        let base = &mut over as *mut matrix3;
        m3::matrix3_scale(base, &src, &(*base).x);
    }
    assert!(
        same_v(over.y, separate.y),
        "shim scale {:?} != {:?}",
        over.y,
        separate.y
    );

    let mut c_separate = OracleMatrix3::default();
    let mut c_over = csrc;
    unsafe {
        c::oracle_matrix3_scale(&mut c_separate, &csrc, &csrc.x);
        let base = &mut c_over as *mut OracleMatrix3;
        c::oracle_matrix3_scale(base, &csrc, &(*base).x);
    }
    assert!(
        same_o(c_over.y, c_separate.y),
        "C scale {:?} != {:?}",
        c_over.y,
        c_separate.y
    );
}

#[test]
fn matrix3_mirrorv_v_aliases_x() {
    let mut src = ident3();
    src.y = v(0.0, 0.0, 1.0);
    src.t = v(2.0, 3.0, 4.0);
    let mut csrc = oident3();
    csrc.y = ov(0.0, 0.0, 1.0);
    csrc.t = ov(2.0, 3.0, 4.0);

    let dir = v(0.0, 0.0, 1.0);
    let mut separate = matrix3::default();
    let mut over = src;
    over.x = dir;
    unsafe {
        m3::matrix3_mirrorv(&mut separate, &src, &dir);
        let base = &mut over as *mut matrix3;
        m3::matrix3_mirrorv(base, &src, &(*base).x);
    }
    assert!(
        same_v(over.y, separate.y),
        "shim mirrorv {:?} != {:?}",
        over.y,
        separate.y
    );

    let cdir = ov(0.0, 0.0, 1.0);
    let mut c_separate = OracleMatrix3::default();
    let mut c_over = csrc;
    c_over.x = cdir;
    unsafe {
        c::oracle_matrix3_mirrorv(&mut c_separate, &csrc, &cdir);
        let base = &mut c_over as *mut OracleMatrix3;
        c::oracle_matrix3_mirrorv(base, &csrc, &(*base).x);
    }
    assert!(
        same_o(c_over.y, c_separate.y),
        "C mirrorv {:?} != {:?}",
        c_over.y,
        c_separate.y
    );
}

#[test]
fn matrix3_mirror_dir_aliases_x() {
    let mut src = ident3();
    src.y = v(0.0, 0.0, 1.0);
    let mut csrc = oident3();
    csrc.y = ov(0.0, 0.0, 1.0);
    let mut separate = matrix3::default();
    let mut over = ident3();
    over.x = v(0.0, 0.0, 1.0);
    unsafe {
        let plane = obs_graphics::ffi::plane::plane {
            dir: v(0.0, 0.0, 1.0),
            dist: 0.0,
        };
        m3::matrix3_mirror(&mut separate, &src, &plane);
        let base = &mut over as *mut matrix3;
        m3::matrix3_mirror(base, &src, base as *const obs_graphics::ffi::plane::plane);
    }
    assert!(
        same_v(over.x, separate.x) && same_v(over.y, separate.y),
        "shim mirror {over:?} != {separate:?}"
    );

    let mut c_separate = OracleMatrix3::default();
    let mut c_over = oident3();
    c_over.x = ov(0.0, 0.0, 1.0);
    unsafe {
        let plane = OraclePlane {
            dir: ov(0.0, 0.0, 1.0),
            dist: 0.0,
        };
        c::oracle_matrix3_mirror(&mut c_separate, &csrc, &plane);
        let base = &mut c_over as *mut OracleMatrix3;
        c::oracle_matrix3_mirror(base, &csrc, base as *const OraclePlane);
    }
    assert!(
        same_o(c_over.x, c_separate.x) && same_o(c_over.y, c_separate.y),
        "C mirror {c_over:?} != {c_separate:?}"
    );
}

#[test]
fn calc_torque_overlap_matches_separate() {
    let v1 = vec3 {
        x: 10.0,
        y: 0.0,
        z: 0.0,
        w: 9.0,
    };
    let v2 = vec3 {
        x: 0.0,
        y: 0.0,
        z: 0.0,
        w: 4.0,
    };
    let mut separate = vec3::default();
    let mut alias = v1;
    // SAFETY: the alias call uses one local as dst and v1. v2 is distinct.
    unsafe {
        mx::calc_torque(&mut separate, &v1, &v2, 0.5, 0.1, 1.0);
        mx::calc_torque(&mut alias, &alias, &v2, 0.5, 0.1, 1.0);
    }
    assert!(
        same_v(alias, separate),
        "shim alias {alias:?} != separate {separate:?}"
    );

    let cv1 = OracleVec3 {
        x: 10.0,
        y: 0.0,
        z: 0.0,
        w: 9.0,
    };
    let cv2 = OracleVec3 {
        x: 0.0,
        y: 0.0,
        z: 0.0,
        w: 4.0,
    };
    let mut c_separate = OracleVec3::default();
    let mut c_alias = cv1;
    unsafe {
        c::oracle_calc_torque(&mut c_separate, &cv1, &cv2, 0.5, 0.1, 1.0);
        c::oracle_calc_torque(&mut c_alias, &c_alias, &cv2, 0.5, 0.1, 1.0);
    }
    assert!(
        same_o(c_alias, c_separate),
        "C alias {c_alias:?} != separate {c_separate:?}"
    );
}

#[test]
fn polar_to_norm_same_address() {
    let polar = vec2 { x: 0.5, y: 0.25 };
    let cpolar = OracleVec2 { x: 0.5, y: 0.25 };
    let mut separate = vec3 {
        x: 9.0,
        y: 9.0,
        z: 9.0,
        w: 7.0,
    };
    let mut over = vec3 {
        x: 0.5,
        y: 0.25,
        z: 9.0,
        w: 7.0,
    };
    unsafe {
        mx::polar_to_norm(&mut separate, &polar);
        let base = &mut over as *mut vec3;
        mx::polar_to_norm(base, base as *const vec2);
    }
    assert!(
        same(over.x, separate.x)
            && same(over.y, separate.y)
            && same(over.z, separate.z)
            && same(over.w, 7.0),
        "shim polar {over:?} != {separate:?}"
    );

    let mut c_separate = OracleVec3 {
        x: 9.0,
        y: 9.0,
        z: 9.0,
        w: 7.0,
    };
    let mut c_over = OracleVec3 {
        x: 0.5,
        y: 0.25,
        z: 9.0,
        w: 7.0,
    };
    unsafe {
        c::oracle_polar_to_norm(&mut c_separate, &cpolar);
        let base = &mut c_over as *mut OracleVec3;
        c::oracle_polar_to_norm(base, base as *const OracleVec2);
    }
    assert!(
        same(c_over.x, c_separate.x)
            && same(c_over.y, c_separate.y)
            && same(c_over.z, c_separate.z)
            && same(c_over.w, 7.0),
        "C polar {c_over:?} != {c_separate:?}"
    );
}
