//! `media-io/format-conversion.c`, twice: `oracle_*` with native SSE2 on
//! x86, `oracle_scalar_*` with the plain-C `__m128i` stand-ins in
//! `oracle/sse-shim` that the non-x86 builds use for both.

macro_rules! format_conversion {
    ($i420:ident, $nv12:ident, $i444:ident, $d420:ident, $dnv12:ident, $d422:ident) => {
        unsafe extern "C" {
            pub fn $i420(
                input: *const u8,
                in_linesize: u32,
                start_y: u32,
                end_y: u32,
                output: *const *mut u8,
                out_linesize: *const u32,
            );
            pub fn $nv12(
                input: *const u8,
                in_linesize: u32,
                start_y: u32,
                end_y: u32,
                output: *const *mut u8,
                out_linesize: *const u32,
            );
            pub fn $i444(
                input: *const u8,
                in_linesize: u32,
                start_y: u32,
                end_y: u32,
                output: *const *mut u8,
                out_linesize: *const u32,
            );
            pub fn $d420(
                input: *const *const u8,
                in_linesize: *const u32,
                start_y: u32,
                end_y: u32,
                output: *mut u8,
                out_linesize: u32,
            );
            pub fn $dnv12(
                input: *const *const u8,
                in_linesize: *const u32,
                start_y: u32,
                end_y: u32,
                output: *mut u8,
                out_linesize: u32,
            );
            pub fn $d422(
                input: *const u8,
                in_linesize: u32,
                start_y: u32,
                end_y: u32,
                output: *mut u8,
                out_linesize: u32,
                leading_lum: bool,
            );
        }
    };
}

format_conversion!(
    oracle_compress_uyvx_to_i420,
    oracle_compress_uyvx_to_nv12,
    oracle_convert_uyvx_to_i444,
    oracle_decompress_420,
    oracle_decompress_nv12,
    oracle_decompress_422
);
format_conversion!(
    oracle_scalar_compress_uyvx_to_i420,
    oracle_scalar_compress_uyvx_to_nv12,
    oracle_scalar_convert_uyvx_to_i444,
    oracle_scalar_decompress_420,
    oracle_scalar_decompress_nv12,
    oracle_scalar_decompress_422
);
