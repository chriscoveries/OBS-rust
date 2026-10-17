//! Tier 1: safe-API tests mirroring `test/cmocka/test_vec4.c`, plus edge cases.

use obs_c_oracle as _;
use obs_graphics::matrix4::Matrix4;
use obs_graphics::vec4::Vec4;
use proptest as _;

fn identity() -> Matrix4 {
    Matrix4::new(
        Vec4::new(1.0, 0.0, 0.0, 0.0),
        Vec4::new(0.0, 1.0, 0.0, 0.0),
        Vec4::new(0.0, 0.0, 1.0, 0.0),
        Vec4::new(0.0, 0.0, 0.0, 1.0),
    )
}

/// Rows 1..16, so every product and sum below is exact.
fn counting_matrix() -> Matrix4 {
    Matrix4::new(
        Vec4::new(1.0, 2.0, 3.0, 4.0),
        Vec4::new(5.0, 6.0, 7.0, 8.0),
        Vec4::new(9.0, 10.0, 11.0, 12.0),
        Vec4::new(13.0, 14.0, 15.0, 16.0),
    )
}

#[test]
fn test_vec4_from_vec3() {
    assert_eq!(
        Vec4::from_xyz(1.5, -2.0, 3.25),
        Vec4::new(1.5, -2.0, 3.25, 1.0)
    );
}

#[test]
fn test_vec4_transform_identity() {
    let v = Vec4::new(1.5, -2.0, 3.25, 0.5);
    assert_eq!(v.transform(&identity()), v);
}

#[test]
fn test_vec4_transform_rows() {
    let m = counting_matrix();
    assert_eq!(
        Vec4::new(1.0, 0.0, 0.0, 0.0).transform(&m),
        Vec4::new(1.0, 2.0, 3.0, 4.0)
    );
    assert_eq!(
        Vec4::new(0.0, 0.0, 0.0, 1.0).transform(&m),
        Vec4::new(13.0, 14.0, 15.0, 16.0)
    );
    assert_eq!(
        Vec4::new(1.0, 1.0, 1.0, 1.0).transform(&m),
        Vec4::new(28.0, 32.0, 36.0, 40.0)
    );
    assert_eq!(
        Vec4::new(1.0, -1.0, 2.0, 0.5).transform(&m),
        Vec4::new(20.5, 23.0, 25.5, 28.0)
    );
}

#[test]
fn test_vec4_transform_translation() {
    let mut m = identity();
    m.t = Vec4::new(10.0, 20.0, 30.0, 1.0);
    assert_eq!(
        Vec4::new(1.0, 2.0, 3.0, 1.0).transform(&m),
        Vec4::new(11.0, 22.0, 33.0, 1.0)
    );
    assert_eq!(
        Vec4::new(1.0, 2.0, 3.0, 0.0).transform(&m),
        Vec4::new(1.0, 2.0, 3.0, 0.0)
    );
}

#[test]
fn transpose_swaps_rows_and_columns() {
    let t = counting_matrix().transpose();
    assert_eq!(t.x, Vec4::new(1.0, 5.0, 9.0, 13.0));
    assert_eq!(t.t, Vec4::new(4.0, 8.0, 12.0, 16.0));
    assert_eq!(t.transpose(), counting_matrix());
}

/// The sum order is observable: `1e8 + 1 - 1e8` loses the 1 depending on
/// which pair is added first. SSE adds (w + y) + (z + x).
#[test]
fn dot_sums_in_sse_order() {
    let ones = Vec4::new(1.0, 1.0, 1.0, 1.0);
    // (w + y) + (z + x) = (-1e8 + 1e8) + (1 + 0) = 1
    assert_eq!(Vec4::new(0.0, 1e8, 1.0, -1e8).dot(ones), 1.0);
    // (w + y) + (z + x) = (1 + 1e8) + (-1e8 + 0) = 0, because 1e8 + 1
    // rounds to 1e8 in f32.
    assert_eq!(Vec4::new(0.0, 1e8, -1e8, 1.0).dot(ones), 0.0);
}
