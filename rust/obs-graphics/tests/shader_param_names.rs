//! Tier 1: 1:1 port of `test/cmocka/test_shader_param_names.c`, through the
//! C ABI shims.

use std::ffi::{CString, c_int};

use obs_c_oracle as _; // test bmem
use obs_graphics::ffi::shader_param_names::{
    get_address_mode, get_sample_filter, get_shader_param_type,
};
use obs_graphics::shader_param_names::{address, filter, param};

fn call(f: unsafe extern "C" fn(*const std::ffi::c_char) -> c_int, s: &str) -> i32 {
    let c = CString::new(s).unwrap();
    // SAFETY: `c` is a valid NUL-terminated string.
    unsafe { f(c.as_ptr()) }
}

fn ty(s: &str) -> i32 {
    call(get_shader_param_type, s)
}

fn sf(s: &str) -> i32 {
    call(get_sample_filter, s)
}

fn am(s: &str) -> i32 {
    call(get_address_mode, s)
}

#[test]
fn test_shader_param_type() {
    assert_eq!(ty("float"), param::FLOAT);
    assert_eq!(ty("float2"), param::VEC2);
    assert_eq!(ty("float3"), param::VEC3);
    assert_eq!(ty("float4"), param::VEC4);
    assert_eq!(ty("int2"), param::INT2);
    assert_eq!(ty("int3"), param::INT3);
    assert_eq!(ty("int4"), param::INT4);
    assert_eq!(ty("float4x4"), param::MATRIX4X4);
    assert_eq!(ty("bool"), param::BOOL);
    assert_eq!(ty("int"), param::INT);
    assert_eq!(ty("string"), param::STRING);

    assert_eq!(ty("texture2d"), param::TEXTURE);
    assert_eq!(ty("texture3d"), param::TEXTURE);
    assert_eq!(ty("texture_rect"), param::TEXTURE);
    // characterized, not endorsed: anything that starts with "texture" is a
    // texture, also "texture" itself and "textures"
    assert_eq!(ty("texture"), param::TEXTURE);
    assert_eq!(ty("textures"), param::TEXTURE);

    // exact, case-sensitive matches only
    assert_eq!(ty("Float"), param::UNKNOWN);
    assert_eq!(ty("float5"), param::UNKNOWN);
    assert_eq!(ty("float4x3"), param::UNKNOWN);
    assert_eq!(ty("Texture2d"), param::UNKNOWN);
    assert_eq!(ty("textur"), param::UNKNOWN);
    assert_eq!(ty(""), param::UNKNOWN);
    assert_eq!(ty("half"), param::UNKNOWN);
}

#[test]
fn test_sample_filter() {
    // the short names match case-insensitively
    assert_eq!(sf("Anisotropy"), filter::ANISOTROPIC);
    assert_eq!(sf("anisotropy"), filter::ANISOTROPIC);
    assert_eq!(sf("Point"), filter::POINT);
    assert_eq!(sf("POINT"), filter::POINT);
    assert_eq!(sf("Linear"), filter::LINEAR);
    assert_eq!(sf("linear"), filter::LINEAR);

    // the D3D names match exactly
    assert_eq!(sf("MIN_MAG_MIP_POINT"), filter::POINT);
    assert_eq!(sf("MIN_MAG_MIP_LINEAR"), filter::LINEAR);
    assert_eq!(
        sf("MIN_MAG_POINT_MIP_LINEAR"),
        filter::MIN_MAG_POINT_MIP_LINEAR
    );
    assert_eq!(
        sf("MIN_POINT_MAG_LINEAR_MIP_POINT"),
        filter::MIN_POINT_MAG_LINEAR_MIP_POINT
    );
    assert_eq!(
        sf("MIN_POINT_MAG_MIP_LINEAR"),
        filter::MIN_POINT_MAG_MIP_LINEAR
    );
    assert_eq!(
        sf("MIN_LINEAR_MAG_MIP_POINT"),
        filter::MIN_LINEAR_MAG_MIP_POINT
    );
    assert_eq!(
        sf("MIN_LINEAR_MAG_POINT_MIP_LINEAR"),
        filter::MIN_LINEAR_MAG_POINT_MIP_LINEAR
    );
    assert_eq!(
        sf("MIN_MAG_LINEAR_MIP_POINT"),
        filter::MIN_MAG_LINEAR_MIP_POINT
    );

    // characterized, not endorsed: a D3D name in another case, and any
    // unknown name, fall back to linear
    assert_eq!(sf("min_mag_mip_point"), filter::LINEAR);
    assert_eq!(sf("min_mag_point_mip_linear"), filter::LINEAR);
    assert_eq!(sf("Bilinear"), filter::LINEAR);
    assert_eq!(sf(""), filter::LINEAR);
}

#[test]
fn test_address_mode() {
    // all case-insensitive
    assert_eq!(am("Wrap"), address::WRAP);
    assert_eq!(am("REPEAT"), address::WRAP);
    assert_eq!(am("clamp"), address::CLAMP);
    assert_eq!(am("None"), address::CLAMP);
    assert_eq!(am("Mirror"), address::MIRROR);
    assert_eq!(am("border"), address::BORDER);
    assert_eq!(am("MirrorOnce"), address::MIRRORONCE);
    assert_eq!(am("MIRRORONCE"), address::MIRRORONCE);

    // unknown names fall back to clamp; whole-string matches only
    assert_eq!(am("Mirror Once"), address::CLAMP);
    assert_eq!(am("Wrapped"), address::CLAMP);
    assert_eq!(am(""), address::CLAMP);
}
