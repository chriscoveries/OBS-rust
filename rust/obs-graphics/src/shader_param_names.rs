//! Safe core for `libobs/graphics/shader-param-names.c`: the shader-parser's
//! string-to-enum lookups. Values are the C enum values.
//!
//! `astrcmpi` is an ASCII case-insensitive comparison of whole strings and
//! `astrcmp_n(s, "texture", 7)` a case-sensitive prefix test; plain `strcmp`
//! is exact.

/// `enum gs_shader_param_type`.
pub mod param {
    pub const UNKNOWN: i32 = 0;
    pub const BOOL: i32 = 1;
    pub const FLOAT: i32 = 2;
    pub const INT: i32 = 3;
    pub const STRING: i32 = 4;
    pub const VEC2: i32 = 5;
    pub const VEC3: i32 = 6;
    pub const VEC4: i32 = 7;
    pub const INT2: i32 = 8;
    pub const INT3: i32 = 9;
    pub const INT4: i32 = 10;
    pub const MATRIX4X4: i32 = 11;
    pub const TEXTURE: i32 = 12;
}

/// `enum gs_sample_filter`.
pub mod filter {
    pub const POINT: i32 = 0;
    pub const LINEAR: i32 = 1;
    pub const ANISOTROPIC: i32 = 2;
    pub const MIN_MAG_POINT_MIP_LINEAR: i32 = 3;
    pub const MIN_POINT_MAG_LINEAR_MIP_POINT: i32 = 4;
    pub const MIN_POINT_MAG_MIP_LINEAR: i32 = 5;
    pub const MIN_LINEAR_MAG_MIP_POINT: i32 = 6;
    pub const MIN_LINEAR_MAG_POINT_MIP_LINEAR: i32 = 7;
    pub const MIN_MAG_LINEAR_MIP_POINT: i32 = 8;
}

/// `enum gs_address_mode`.
pub mod address {
    pub const CLAMP: i32 = 0;
    pub const WRAP: i32 = 1;
    pub const MIRROR: i32 = 2;
    pub const BORDER: i32 = 3;
    pub const MIRRORONCE: i32 = 4;
}

/// `get_shader_param_type`.
pub fn shader_param_type(ty: &[u8]) -> i32 {
    match ty {
        b"float" => param::FLOAT,
        b"float2" => param::VEC2,
        b"float3" => param::VEC3,
        b"float4" => param::VEC4,
        b"int2" => param::INT2,
        b"int3" => param::INT3,
        b"int4" => param::INT4,
        // astrcmp_n(type, "texture", 7): any type that starts with it.
        t if t.starts_with(b"texture") => param::TEXTURE,
        b"float4x4" => param::MATRIX4X4,
        b"bool" => param::BOOL,
        b"int" => param::INT,
        b"string" => param::STRING,
        _ => param::UNKNOWN,
    }
}

/// `get_sample_filter`. The short names compare case-insensitively, the
/// D3D names exactly; anything else is linear.
pub fn sample_filter(f: &[u8]) -> i32 {
    if f.eq_ignore_ascii_case(b"Anisotropy") {
        return filter::ANISOTROPIC;
    }
    if f.eq_ignore_ascii_case(b"Point") || f == b"MIN_MAG_MIP_POINT" {
        return filter::POINT;
    }
    if f.eq_ignore_ascii_case(b"Linear") || f == b"MIN_MAG_MIP_LINEAR" {
        return filter::LINEAR;
    }
    match f {
        b"MIN_MAG_POINT_MIP_LINEAR" => filter::MIN_MAG_POINT_MIP_LINEAR,
        b"MIN_POINT_MAG_LINEAR_MIP_POINT" => filter::MIN_POINT_MAG_LINEAR_MIP_POINT,
        b"MIN_POINT_MAG_MIP_LINEAR" => filter::MIN_POINT_MAG_MIP_LINEAR,
        b"MIN_LINEAR_MAG_MIP_POINT" => filter::MIN_LINEAR_MAG_MIP_POINT,
        b"MIN_LINEAR_MAG_POINT_MIP_LINEAR" => filter::MIN_LINEAR_MAG_POINT_MIP_LINEAR,
        b"MIN_MAG_LINEAR_MIP_POINT" => filter::MIN_MAG_LINEAR_MIP_POINT,
        _ => filter::LINEAR,
    }
}

/// `get_address_mode`: every name compares case-insensitively; anything
/// else is clamp.
pub fn address_mode(m: &[u8]) -> i32 {
    let is = |name: &[u8]| m.eq_ignore_ascii_case(name);
    if is(b"Wrap") || is(b"Repeat") {
        address::WRAP
    } else if is(b"Clamp") || is(b"None") {
        address::CLAMP
    } else if is(b"Mirror") {
        address::MIRROR
    } else if is(b"Border") {
        address::BORDER
    } else if is(b"MirrorOnce") {
        address::MIRRORONCE
    } else {
        address::CLAMP
    }
}
