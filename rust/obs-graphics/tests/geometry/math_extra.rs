//! Tier 1: safe-API tests mirroring `test/cmocka/test_math_extra.c`, plus
//! edge cases. The aliased `calc_torque` case is in `math_extra_parity`:
//! the safe core reads both inputs before it writes, and the C test calls
//! the exported function.

use obs_c_oracle as _;
use obs_graphics::math_extra::{self, calc_torque, calc_torquef, rand_float};
use obs_graphics::vec3::Vec3;

fn assert_xyz(v: Vec3, x: f32, y: f32, z: f32) {
    assert_eq!((v.x, v.y, v.z), (x, y, z), "{v:?}");
}

fn assert_vec(v: Vec3, x: f32, y: f32, z: f32, w: f32) {
    assert_eq!((v.x, v.y, v.z, v.w), (x, y, z, w), "{v:?}");
}

#[test]
fn test_polar_to_cart_plus_z() {
    let cart = math_extra::polar_to_cart(Vec3::with_w(0.0, 0.0, 2.0, 0.0));
    assert_xyz(cart, 0.0, 0.0, 2.0);
    assert_eq!(cart.w, 0.0);
}

#[test]
fn test_polar_to_cart_zero_radius() {
    let cart = math_extra::polar_to_cart(Vec3::with_w(1.0, 2.0, 0.0, 9.0));
    assert_xyz(cart, 0.0, 0.0, 0.0);
    assert_eq!(cart.w, 0.0);
}

#[test]
fn test_polar_to_cart_alias() {
    let v = Vec3::with_w(0.0, 0.0, 2.0, 0.0);
    assert_xyz(math_extra::polar_to_cart(v), 0.0, 0.0, 2.0);
}

#[test]
fn test_cart_to_polar_within_epsilon() {
    assert_vec(
        math_extra::cart_to_polar(Vec3::with_w(0.0, 0.0, 0.0, 1e-4)),
        0.0,
        0.0,
        0.0,
        0.0,
    );
    assert_vec(
        math_extra::cart_to_polar(Vec3::with_w(1e-4, 0.0, 0.0, 0.0)),
        0.0,
        0.0,
        0.0,
        0.0,
    );
}

#[test]
fn test_cart_to_polar_plus_z() {
    let polar = math_extra::cart_to_polar(Vec3::with_w(0.0, 0.0, 2.0, 0.0));
    assert_xyz(polar, 0.0, 0.0, 2.0);
    assert_eq!(polar.w, 0.0);
}

#[test]
fn test_cart_to_polar_w_lane() {
    let polar = math_extra::cart_to_polar(Vec3::with_w(0.0, 0.0, 0.0, 2e-4));
    assert_xyz(polar, 0.0, 0.0, 2e-4);
    assert_eq!(polar.w, 0.0);
}

#[test]
fn test_norm_polar_roundtrip_plus_z() {
    let polar = math_extra::norm_to_polar(Vec3::with_w(0.0, 0.0, 1.0, 4.0));
    assert_eq!((polar.x, polar.y), (0.0, 0.0));
    // The safe result clears w. The shim test checks that dst->w stays 7.
    assert_vec(math_extra::polar_to_norm(polar), 0.0, 0.0, 1.0, 0.0);
}

#[test]
fn test_calc_torquef_close_returns_target() {
    assert_eq!(calc_torquef(1.0, 1.00005, 1.0, 0.1, 1.0), 1.00005);
}

#[test]
fn test_calc_torquef_steps_and_clamps() {
    assert_eq!(calc_torquef(0.0, 10.0, 0.5, 0.1, 1.0), 5.0);
    assert_eq!(calc_torquef(0.0, 10.0, 0.001, 0.5, 1.0), 0.5);
    assert_eq!(calc_torquef(0.0, 1.0, 1.0, 0.1, 10.0), 1.0);
    assert_eq!(calc_torquef(10.0, 0.0, 0.5, 0.1, 1.0), 5.0);
    assert_eq!(calc_torquef(10.0, 0.0, 0.001, 0.5, 1.0), 9.5);
    assert_eq!(calc_torquef(1.0, 0.0, 1.0, 0.1, 10.0), 0.0);
}

#[test]
fn test_calc_torque_one_sided_close_copies_target() {
    let v1 = Vec3::with_w(0.0, 0.0, 0.0, 1.0);
    let v2 = Vec3::with_w(4.0, 2.0, 8.0, 5.0);
    assert_vec(calc_torque(v1, v2, 0.5, 0.1, 1.0), 4.0, 2.0, 8.0, 5.0);
}

#[test]
fn test_calc_torque_moves_and_snaps() {
    let v2 = Vec3::with_w(0.0, 0.0, 0.0, 4.0);
    assert_vec(
        calc_torque(Vec3::with_w(10.0, 0.0, 0.0, 9.0), v2, 0.5, 0.1, 1.0),
        5.0,
        0.0,
        0.0,
        0.0,
    );
    assert_vec(
        calc_torque(Vec3::new(1.0, 0.0, 0.0), v2, 0.001, 0.5, 1.0),
        0.5,
        0.0,
        0.0,
        0.0,
    );
    assert_vec(
        calc_torque(Vec3::with_w(10.0, 0.0, 0.0, 9.0), v2, 0.5, 0.1, 10.0),
        0.0,
        0.0,
        0.0,
        4.0,
    );
}

#[test]
fn test_rand_float_unit_interval() {
    assert_eq!(rand_float(true, 0.25), 0.25);
    assert_eq!(rand_float(false, 0.25), -0.5);
    assert_eq!(rand_float(true, 0.0), 0.0);
    assert_eq!(rand_float(false, 0.0), -1.0);
    assert_eq!(rand_float(true, 1.0), 1.0);
    assert_eq!(rand_float(false, 1.0), 1.0);
}
