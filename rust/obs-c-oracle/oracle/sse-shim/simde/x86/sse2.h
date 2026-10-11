/* Test-only stand-in for <simde/x86/sse2.h>, used by the C oracle so that
 * `cargo test` does not need SIMDe installed. libobs/util/sse-intrin.h
 * includes SIMDe everywhere except MSVC/MinGW on x86.
 *
 * On x86 this is the native SSE header, which is what SIMDe maps to there.
 * Elsewhere it defines, in plain C, only the intrinsics the graphics math
 * headers and media-io/format-conversion.c use. Each one follows the SSE
 * definition lane by lane, which is also what SIMDe guarantees on NEON
 * (including the NaN and signed-zero rules of minps/maxps). The oracle is
 * built with -ffp-contract=off, so no multiply-add is fused.
 *
 * Defining OBS_ORACLE_SCALAR_SSE selects the plain-C versions on x86 too
 * (oracle/format_conversion_scalar.c), so x86 parity runs check them. */
#pragma once

#if (defined(__x86_64__) || defined(__i386__)) && !defined(OBS_ORACLE_SCALAR_SSE)
#include <emmintrin.h>
#else

#include <stdint.h>
#include <string.h>

typedef struct {
	_Alignas(16) float f[4];
} __m128;

#define _MM_SHUFFLE(z, y, x, w) (((z) << 6) | ((y) << 4) | ((x) << 2) | (w))

static inline __m128 oracle_sse_make(float a, float b, float c, float d)
{
	__m128 r;
	r.f[0] = a;
	r.f[1] = b;
	r.f[2] = c;
	r.f[3] = d;
	return r;
}

static inline __m128 _mm_setzero_ps(void)
{
	return oracle_sse_make(0.0f, 0.0f, 0.0f, 0.0f);
}

static inline __m128 _mm_set1_ps(float f)
{
	return oracle_sse_make(f, f, f, f);
}

/* Arguments are high lane first. */
static inline __m128 _mm_set_ps(float w, float z, float y, float x)
{
	return oracle_sse_make(x, y, z, w);
}

static inline __m128 _mm_add_ps(__m128 a, __m128 b)
{
	return oracle_sse_make(a.f[0] + b.f[0], a.f[1] + b.f[1], a.f[2] + b.f[2], a.f[3] + b.f[3]);
}

static inline __m128 _mm_sub_ps(__m128 a, __m128 b)
{
	return oracle_sse_make(a.f[0] - b.f[0], a.f[1] - b.f[1], a.f[2] - b.f[2], a.f[3] - b.f[3]);
}

static inline __m128 _mm_mul_ps(__m128 a, __m128 b)
{
	return oracle_sse_make(a.f[0] * b.f[0], a.f[1] * b.f[1], a.f[2] * b.f[2], a.f[3] * b.f[3]);
}

static inline __m128 _mm_div_ps(__m128 a, __m128 b)
{
	return oracle_sse_make(a.f[0] / b.f[0], a.f[1] / b.f[1], a.f[2] / b.f[2], a.f[3] / b.f[3]);
}

/* minps/maxps return the second operand unless the comparison holds, so a
 * NaN in either lane, or equal zeros of either sign, give `b`. */
static inline float oracle_sse_min(float a, float b)
{
	return a < b ? a : b;
}

static inline float oracle_sse_max(float a, float b)
{
	return a > b ? a : b;
}

static inline __m128 _mm_min_ps(__m128 a, __m128 b)
{
	return oracle_sse_make(oracle_sse_min(a.f[0], b.f[0]), oracle_sse_min(a.f[1], b.f[1]),
			       oracle_sse_min(a.f[2], b.f[2]), oracle_sse_min(a.f[3], b.f[3]));
}

static inline __m128 _mm_max_ps(__m128 a, __m128 b)
{
	return oracle_sse_make(oracle_sse_max(a.f[0], b.f[0]), oracle_sse_max(a.f[1], b.f[1]),
			       oracle_sse_max(a.f[2], b.f[2]), oracle_sse_max(a.f[3], b.f[3]));
}

static inline __m128 _mm_movehl_ps(__m128 a, __m128 b)
{
	return oracle_sse_make(b.f[2], b.f[3], a.f[2], a.f[3]);
}

static inline __m128 _mm_movelh_ps(__m128 a, __m128 b)
{
	return oracle_sse_make(a.f[0], a.f[1], b.f[0], b.f[1]);
}

static inline __m128 _mm_unpacklo_ps(__m128 a, __m128 b)
{
	return oracle_sse_make(a.f[0], b.f[0], a.f[1], b.f[1]);
}

static inline __m128 _mm_unpackhi_ps(__m128 a, __m128 b)
{
	return oracle_sse_make(a.f[2], b.f[2], a.f[3], b.f[3]);
}

static inline __m128 _mm_shuffle_ps(__m128 a, __m128 b, int imm)
{
	return oracle_sse_make(a.f[imm & 3], a.f[(imm >> 2) & 3], b.f[(imm >> 4) & 3], b.f[(imm >> 6) & 3]);
}

/* __m128i holds its 16 bytes in register (little-endian lane) order. The
 * storage is declared as uint32_t because format-conversion.c reads lanes
 * with *(uint32_t *)&val; the intrinsics go through memcpy. */
typedef struct {
	_Alignas(16) uint32_t u32[4];
} __m128i;

static inline void oracle_sse_bytes(uint8_t b[16], __m128i a)
{
	memcpy(b, a.u32, 16);
}

static inline __m128i oracle_sse_from_bytes(const uint8_t b[16])
{
	__m128i r;
	memcpy(r.u32, b, 16);
	return r;
}

static inline int16_t oracle_sse_lane16(const uint8_t b[16], int i)
{
	return (int16_t)(uint16_t)(b[2 * i] | b[2 * i + 1] << 8);
}

static inline void oracle_sse_set_lane16(uint8_t b[16], int i, int16_t v)
{
	b[2 * i] = (uint8_t)((uint16_t)v & 0xFF);
	b[2 * i + 1] = (uint8_t)((uint16_t)v >> 8);
}

static inline uint32_t oracle_sse_lane32(const uint8_t b[16], int i)
{
	return (uint32_t)b[4 * i] | (uint32_t)b[4 * i + 1] << 8 | (uint32_t)b[4 * i + 2] << 16 |
	       (uint32_t)b[4 * i + 3] << 24;
}

static inline void oracle_sse_set_lane32(uint8_t b[16], int i, uint32_t v)
{
	for (int k = 0; k < 4; k++) {
		b[4 * i + k] = (uint8_t)(v >> (8 * k));
	}
}

static inline uint64_t oracle_sse_lane64(const uint8_t b[16], int i)
{
	return (uint64_t)oracle_sse_lane32(b, 2 * i) | (uint64_t)oracle_sse_lane32(b, 2 * i + 1) << 32;
}

static inline void oracle_sse_set_lane64(uint8_t b[16], int i, uint64_t v)
{
	oracle_sse_set_lane32(b, 2 * i, (uint32_t)v);
	oracle_sse_set_lane32(b, 2 * i + 1, (uint32_t)(v >> 32));
}

static inline __m128i _mm_load_si128(const __m128i *p)
{
	__m128i r;
	memcpy(r.u32, p, 16);
	return r;
}

static inline __m128i _mm_set1_epi32(int a)
{
	uint8_t r[16];
	for (int i = 0; i < 4; i++) {
		oracle_sse_set_lane32(r, i, (uint32_t)a);
	}
	return oracle_sse_from_bytes(r);
}

static inline __m128i _mm_set1_epi16(short a)
{
	uint8_t r[16];
	for (int i = 0; i < 8; i++) {
		oracle_sse_set_lane16(r, i, a);
	}
	return oracle_sse_from_bytes(r);
}

static inline __m128i _mm_and_si128(__m128i a, __m128i b)
{
	uint8_t x[16], y[16];
	oracle_sse_bytes(x, a);
	oracle_sse_bytes(y, b);
	for (int i = 0; i < 16; i++) {
		x[i] &= y[i];
	}
	return oracle_sse_from_bytes(x);
}

/* Shifts the whole register right by imm bytes, shifting in zeros. */
static inline __m128i _mm_srli_si128(__m128i a, int imm)
{
	uint8_t x[16], r[16];
	oracle_sse_bytes(x, a);
	for (int i = 0; i < 16; i++) {
		r[i] = (imm >= 0 && imm < 16 - i) ? x[i + imm] : 0;
	}
	return oracle_sse_from_bytes(r);
}

static inline int16_t oracle_sse_sat16(int32_t v)
{
	return (int16_t)(v < INT16_MIN ? INT16_MIN : v > INT16_MAX ? INT16_MAX : v);
}

static inline uint8_t oracle_sse_satu8(int16_t v)
{
	return (uint8_t)(v < 0 ? 0 : v > 255 ? 255 : v);
}

/* The four int32 lanes of a, then of b, saturated to int16. */
static inline __m128i _mm_packs_epi32(__m128i a, __m128i b)
{
	uint8_t x[16], y[16], r[16];
	oracle_sse_bytes(x, a);
	oracle_sse_bytes(y, b);
	for (int i = 0; i < 4; i++) {
		oracle_sse_set_lane16(r, i, oracle_sse_sat16((int32_t)oracle_sse_lane32(x, i)));
		oracle_sse_set_lane16(r, i + 4, oracle_sse_sat16((int32_t)oracle_sse_lane32(y, i)));
	}
	return oracle_sse_from_bytes(r);
}

/* The eight int16 lanes of a, then of b, saturated to uint8. */
static inline __m128i _mm_packus_epi16(__m128i a, __m128i b)
{
	uint8_t x[16], y[16], r[16];
	oracle_sse_bytes(x, a);
	oracle_sse_bytes(y, b);
	for (int i = 0; i < 8; i++) {
		r[i] = oracle_sse_satu8(oracle_sse_lane16(x, i));
		r[i + 8] = oracle_sse_satu8(oracle_sse_lane16(y, i));
	}
	return oracle_sse_from_bytes(r);
}

static inline __m128i _mm_add_epi64(__m128i a, __m128i b)
{
	uint8_t x[16], y[16];
	oracle_sse_bytes(x, a);
	oracle_sse_bytes(y, b);
	for (int i = 0; i < 2; i++) {
		oracle_sse_set_lane64(x, i, oracle_sse_lane64(x, i) + oracle_sse_lane64(y, i));
	}
	return oracle_sse_from_bytes(x);
}

static inline __m128i _mm_shuffle_epi32(__m128i a, int imm)
{
	uint8_t x[16], r[16];
	oracle_sse_bytes(x, a);
	for (int i = 0; i < 4; i++) {
		oracle_sse_set_lane32(r, i, oracle_sse_lane32(x, (imm >> (2 * i)) & 3));
	}
	return oracle_sse_from_bytes(r);
}

/* Shuffles the low four int16 lanes; the high four are copied. */
static inline __m128i _mm_shufflelo_epi16(__m128i a, int imm)
{
	uint8_t x[16], r[16];
	oracle_sse_bytes(x, a);
	memcpy(r, x, 16);
	for (int i = 0; i < 4; i++) {
		oracle_sse_set_lane16(r, i, oracle_sse_lane16(x, (imm >> (2 * i)) & 3));
	}
	return oracle_sse_from_bytes(r);
}

/* Arithmetic shift of each int16 lane; counts above 15 fill with the sign. */
static inline __m128i _mm_srai_epi16(__m128i a, int imm)
{
	uint8_t x[16];
	int n = imm > 15 ? 15 : imm;
	oracle_sse_bytes(x, a);
	for (int i = 0; i < 8; i++) {
		int v = oracle_sse_lane16(x, i);
		oracle_sse_set_lane16(x, i, (int16_t)(v < 0 ? ~(~v >> n) : v >> n));
	}
	return oracle_sse_from_bytes(x);
}

#endif
