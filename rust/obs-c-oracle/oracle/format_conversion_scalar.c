/* Test-only oracle: format_conversion.c again, renamed to oracle_scalar_*
 * and built with OBS_ORACLE_SCALAR_SSE, so on x86 it uses the plain-C
 * __m128i stand-ins in sse-shim that non-x86 builds rely on. Parity checks
 * them against native SSE2 there. MSVC's sse-intrin.h always takes
 * <emmintrin.h>, so on MSVC this is native SSE2 too. */
#define OBS_ORACLE_SCALAR_SSE

#define compress_uyvx_to_i420 oracle_scalar_compress_uyvx_to_i420
#define compress_uyvx_to_nv12 oracle_scalar_compress_uyvx_to_nv12
#define convert_uyvx_to_i444 oracle_scalar_convert_uyvx_to_i444
#define decompress_nv12 oracle_scalar_decompress_nv12
#define decompress_420 oracle_scalar_decompress_420
#define decompress_422 oracle_scalar_decompress_422

#include "media-io/format-conversion.c.h"
