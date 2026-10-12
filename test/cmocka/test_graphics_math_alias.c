#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <stdint.h>
#include <string.h>
#include <cmocka.h>

#include <util/c99defs.h>
#include <graphics/bounds.h>
#include <graphics/math-extra.h>
#include <graphics/matrix3.h>
#include <graphics/matrix4.h>
#include <graphics/plane.h>
#include <graphics/quat.h>
#include <graphics/srgb.h>
#include <graphics/vec2.h>
#include <graphics/vec3.h>
#include <graphics/vec4.h>

/* Alias safety of the exported libobs graphics math functions: every one of
 * these must return the same result whether or not `dst` overlaps an input.
 * Each test runs the function twice, once on separate storage and once on the
 * overlapping arrangement from the 2026-10-12 alias audit, and asserts exact
 * equality (the fixes keep the non-overlapping arithmetic untouched, so the
 * two paths must match bit for bit). */

static void assert_vec3_same(const struct vec3 *a, const struct vec3 *b)
{
	assert_true(a->x == b->x);
	assert_true(a->y == b->y);
	assert_true(a->z == b->z);
	assert_true(a->w == b->w);
}

static void set_v3(struct vec3 *v, float x, float y, float z)
{
	vec3_set(v, x, y, z);
	v->w = 0.0f;
}

static void set_v4(struct vec4 *v, float x, float y, float z)
{
	vec4_set(v, x, y, z, 0.0f);
}

/* 90-degree rotation about Z: positive-trace branch of quat_from_matrix4. */
static struct matrix4 rot90z_m4(void)
{
	struct matrix4 m;
	set_v4(&m.x, 0.0f, 1.0f, 0.0f);
	set_v4(&m.y, -1.0f, 0.0f, 0.0f);
	set_v4(&m.z, 0.0f, 0.0f, 1.0f);
	set_v4(&m.t, 0.0f, 0.0f, 0.0f);
	m.t.w = 1.0f;
	return m;
}

/* 180-degree rotation about Y: negative-trace branch, diagonal index 1. */
static struct matrix4 rot180y_m4(void)
{
	struct matrix4 m;
	set_v4(&m.x, -1.0f, 0.0f, 0.0f);
	set_v4(&m.y, 0.0f, 1.0f, 0.0f);
	set_v4(&m.z, 0.0f, 0.0f, -1.0f);
	set_v4(&m.t, 0.0f, 0.0f, 0.0f);
	m.t.w = 1.0f;
	return m;
}

static struct matrix3 rot90z_m3(void)
{
	struct matrix3 m;
	set_v3(&m.x, 0.0f, 1.0f, 0.0f);
	set_v3(&m.y, -1.0f, 0.0f, 0.0f);
	set_v3(&m.z, 0.0f, 0.0f, 1.0f);
	set_v3(&m.t, 0.0f, 0.0f, 0.0f);
	return m;
}

static void test_quat_mul_dst_aliases_q1(void **state)
{
	UNUSED_PARAMETER(state);

	const struct quat a = {0.2f, -0.3f, 0.4f, 0.8f};
	const struct quat b = {-0.5f, 0.6f, -0.7f, 0.9f};
	struct quat ref, dst;

	quat_mul(&ref, &a, &b);

	dst = a;
	quat_mul(&dst, &dst, &b);
	assert_memory_equal(&dst, &ref, sizeof(dst));
}

static void test_quat_mul_dst_aliases_q2(void **state)
{
	UNUSED_PARAMETER(state);

	const struct quat a = {0.2f, -0.3f, 0.4f, 0.8f};
	const struct quat b = {-0.5f, 0.6f, -0.7f, 0.9f};
	struct quat ref, dst;

	quat_mul(&ref, &a, &b);

	dst = b;
	quat_mul(&dst, &a, &dst);
	assert_memory_equal(&dst, &ref, sizeof(dst));
}

static void test_quat_mul_dst_aliases_both(void **state)
{
	UNUSED_PARAMETER(state);

	const struct quat a = {0.2f, -0.3f, 0.4f, 0.8f};
	struct quat ref, dst;

	quat_mul(&ref, &a, &a);

	dst = a;
	quat_mul(&dst, &dst, &dst);
	assert_memory_equal(&dst, &ref, sizeof(dst));
}

/* `dst` sharing storage with the source matrix, through the matrix3 cast and
 * through a matrix4. Both quat_from_matrix* branches write lanes that later
 * reads, unless the matrix is snapshotted first. */
static void test_quat_from_matrix4_same_address(void **state)
{
	UNUSED_PARAMETER(state);

	const struct matrix4 m = rot90z_m4();
	struct quat ref;
	union {
		struct matrix4 m;
		struct quat q;
	} shared;

	quat_from_matrix4(&ref, &m);

	shared.m = m;
	quat_from_matrix4(&shared.q, &shared.m);
	assert_memory_equal(&shared.q, &ref, sizeof(shared.q));
}

static void test_quat_from_matrix4_same_address_negative_trace(void **state)
{
	UNUSED_PARAMETER(state);

	const struct matrix4 m = rot180y_m4();
	struct quat ref;
	union {
		struct matrix4 m;
		struct quat q;
	} shared;

	quat_from_matrix4(&ref, &m);

	shared.m = m;
	quat_from_matrix4(&shared.q, &shared.m);
	assert_memory_equal(&shared.q, &ref, sizeof(shared.q));
}

static void test_quat_from_matrix3_same_address(void **state)
{
	UNUSED_PARAMETER(state);

	const struct matrix3 m = rot90z_m3();
	struct quat ref;
	union {
		struct matrix3 m;
		struct quat q;
	} shared;

	quat_from_matrix3(&ref, &m);

	shared.m = m;
	quat_from_matrix3(&shared.q, &shared.m);
	assert_memory_equal(&shared.q, &ref, sizeof(shared.q));
}

static void scale_axes(struct matrix3 *m)
{
	memset(m, 0, sizeof(*m));
	set_v3(&m->x, 1.0f, 2.0f, 3.0f);
	set_v3(&m->y, 4.0f, 5.0f, 6.0f);
	set_v3(&m->z, 7.0f, 8.0f, 9.0f);
	set_v3(&m->t, 0.5f, -1.0f, 2.0f);
}

static void test_vec3_rotate_dst_aliases_matrix_row(void **state)
{
	UNUSED_PARAMETER(state);

	const struct vec3 v = {2.0f, -3.0f, 0.25f, 0.0f};
	struct matrix3 m;
	struct vec3 ref;

	scale_axes(&m);
	vec3_rotate(&ref, &v, &m);

	scale_axes(&m);
	vec3_rotate(&m.x, &v, &m);
	assert_vec3_same(&m.x, &ref);

	scale_axes(&m);
	vec3_rotate(&m.y, &v, &m);
	assert_vec3_same(&m.y, &ref);

	scale_axes(&m);
	vec3_rotate(&m.z, &v, &m);
	assert_vec3_same(&m.z, &ref);

	scale_axes(&m);
	vec3_rotate(&m.t, &v, &m);
	assert_vec3_same(&m.t, &ref);
}

static void test_vec3_transform3x4_dst_aliases_matrix_row(void **state)
{
	UNUSED_PARAMETER(state);

	const struct vec3 v = {2.0f, -3.0f, 0.25f, 0.0f};
	struct matrix3 m;
	struct vec3 ref;

	scale_axes(&m);
	vec3_transform3x4(&ref, &v, &m);

	scale_axes(&m);
	vec3_transform3x4(&m.x, &v, &m);
	assert_vec3_same(&m.x, &ref);

	scale_axes(&m);
	vec3_transform3x4(&m.y, &v, &m);
	assert_vec3_same(&m.y, &ref);

	scale_axes(&m);
	vec3_transform3x4(&m.z, &v, &m);
	assert_vec3_same(&m.z, &ref);

	scale_axes(&m);
	vec3_transform3x4(&m.t, &v, &m);
	assert_vec3_same(&m.t, &ref);
}

static void test_matrix3_scale_vec_aliases_row(void **state)
{
	UNUSED_PARAMETER(state);

	struct matrix3 src, ref, alias;
	struct vec3 v;

	set_v3(&src.x, 2.0f, 3.0f, 4.0f);
	set_v3(&src.y, 5.0f, 6.0f, 7.0f);
	set_v3(&src.z, 9.0f, 10.0f, 11.0f);
	set_v3(&src.t, 13.0f, 14.0f, 15.0f);
	set_v3(&v, 2.0f, -1.0f, 0.5f);

	matrix3_scale(&ref, &src, &v);

	alias = src;
	alias.x = v; /* vector operand lives in the output row */
	matrix3_scale(&alias, &src, &alias.x);
	assert_memory_equal(&alias, &ref, sizeof(alias));
}

static void test_matrix3_mirrorv_vec_aliases_row(void **state)
{
	UNUSED_PARAMETER(state);

	struct matrix3 src, ref, alias;
	struct vec3 v;

	set_v3(&src.x, 2.0f, 1.0f, 0.0f);
	set_v3(&src.y, 1.0f, 2.0f, 3.0f);
	set_v3(&src.z, 4.0f, 5.0f, 6.0f);
	set_v3(&src.t, 7.0f, 8.0f, 9.0f);
	set_v3(&v, 0.6f, 0.8f, 0.0f);

	matrix3_mirrorv(&ref, &src, &v);

	alias = src;
	alias.x = v;
	matrix3_mirrorv(&alias, &src, &alias.x);
	assert_memory_equal(&alias, &ref, sizeof(alias));
}

static void test_matrix3_mirror_plane_same_address(void **state)
{
	UNUSED_PARAMETER(state);

	struct matrix3 src, ref;
	struct plane p;
	union {
		struct matrix3 m;
		struct plane p;
	} shared;

	set_v3(&src.x, 2.0f, 1.0f, 0.0f);
	set_v3(&src.y, 1.0f, 2.0f, 3.0f);
	set_v3(&src.z, 4.0f, 5.0f, 6.0f);
	set_v3(&src.t, 7.0f, 8.0f, 9.0f);
	set_v3(&p.dir, 0.6f, 0.8f, 0.0f);
	p.dist = 2.0f;

	matrix3_mirror(&ref, &src, &p);

	shared.m = src;
	shared.p = p;
	matrix3_mirror(&shared.m, &src, &shared.p);
	assert_memory_equal(&shared.m, &ref, sizeof(shared.m));
}

static void test_plane_from_tri_v1_same_address(void **state)
{
	UNUSED_PARAMETER(state);

	struct vec3 v2, v3;
	struct plane p_alias, p_ref;

	set_v3(&p_alias.dir, 0.0f, 0.0f, 0.0f);
	p_alias.dist = 123.0f;
	p_ref = p_alias;
	set_v3(&v2, 1.0f, 0.0f, 0.0f);
	set_v3(&v3, 0.0f, 1.0f, 0.0f);

	plane_from_tri(&p_ref, &p_alias.dir, &v2, &v3);
	plane_from_tri(&p_alias, &p_alias.dir, &v2, &v3);
	assert_memory_equal(&p_alias, &p_ref, sizeof(p_alias));
}

static void test_bounds_move_vec_aliases_min(void **state)
{
	UNUSED_PARAMETER(state);

	struct bounds src, out, alias;

	set_v3(&src.min, 1.0f, 2.0f, 3.0f);
	set_v3(&src.max, 10.0f, 20.0f, 30.0f);

	bounds_move(&out, &src, &src.min);

	alias = src;
	bounds_move(&alias, &alias, &alias.min);
	assert_memory_equal(&alias, &out, sizeof(alias));
}

static void test_bounds_scale_vec_aliases_min(void **state)
{
	UNUSED_PARAMETER(state);

	struct bounds src, out, alias;

	set_v3(&src.min, 2.0f, 3.0f, 4.0f);
	set_v3(&src.max, 10.0f, 20.0f, 30.0f);

	bounds_scale(&out, &src, &src.min);

	alias = src;
	bounds_scale(&alias, &alias, &alias.min);
	assert_memory_equal(&alias, &out, sizeof(alias));
}

static void test_bounds_merge_point_vec_aliases_min(void **state)
{
	UNUSED_PARAMETER(state);

	struct bounds base, out, alias;
	struct vec3 point;

	set_v3(&base.min, 5.0f, 5.0f, 5.0f);
	set_v3(&base.max, 10.0f, 10.0f, 10.0f);
	set_v3(&point, 20.0f, 20.0f, 20.0f);

	set_v3(&out.min, 20.0f, 20.0f, 20.0f);
	set_v3(&out.max, 30.0f, 30.0f, 30.0f);
	bounds_merge_point(&out, &base, &point);

	set_v3(&alias.min, 20.0f, 20.0f, 20.0f);
	set_v3(&alias.max, 30.0f, 30.0f, 30.0f);
	bounds_merge_point(&alias, &base, &alias.min);

	assert_memory_equal(&alias, &out, sizeof(out));
}

static void test_bounds_get_center_dst_aliases_min(void **state)
{
	UNUSED_PARAMETER(state);

	struct bounds src;
	struct vec3 center;

	set_v3(&src.min, 0.0f, 0.0f, 0.0f);
	set_v3(&src.max, 10.0f, 20.0f, 30.0f);

	bounds_get_center(&center, &src);
	bounds_get_center(&src.min, &src);
	assert_vec3_same(&src.min, &center);
}

static void test_calc_torque_dst_aliases_v1(void **state)
{
	UNUSED_PARAMETER(state);

	struct vec3 v1, v2, ref;

	set_v3(&v1, 10.0f, 0.0f, 0.0f);
	set_v3(&v2, 0.0f, 0.0f, 0.0f);
	ref = v1;

	calc_torque(&ref, &v1, &v2, 0.5f, 0.0f, 0.5f);
	calc_torque(&v1, &v1, &v2, 0.5f, 0.0f, 0.5f);
	assert_vec3_same(&v1, &ref);
}

static void test_polar_to_norm_same_address(void **state)
{
	UNUSED_PARAMETER(state);

	union {
		struct vec3 dst;
		struct vec2 polar;
	} shared;
	struct vec3 ref;

	shared.polar.x = 0.3f;
	shared.polar.y = -0.8f;

	polar_to_norm(&ref, &shared.polar);
	polar_to_norm(&shared.dst, &shared.polar);

	assert_true(shared.dst.x == ref.x);
	assert_true(shared.dst.y == ref.y);
	assert_true(shared.dst.z == ref.z);
}

static void test_gs_u8x4_to_float4_same_base(void **state)
{
	UNUSED_PARAMETER(state);

	const uint8_t src[4] = {64, 128, 192, 255};
	float ref[4];
	union {
		uint8_t u[16];
		float f[4];
	} shared;

	gs_u8x4_to_float4(ref, src);

	memset(&shared, 0, sizeof(shared));
	memcpy(shared.u, src, sizeof(src));
	gs_u8x4_to_float4(shared.f, shared.u);

	assert_true(shared.f[0] == ref[0]);
	assert_true(shared.f[1] == ref[1]);
	assert_true(shared.f[2] == ref[2]);
	assert_true(shared.f[3] == ref[3]);
}

int main(void)
{
	const struct CMUnitTest tests[] = {
		cmocka_unit_test(test_quat_mul_dst_aliases_q1),
		cmocka_unit_test(test_quat_mul_dst_aliases_q2),
		cmocka_unit_test(test_quat_mul_dst_aliases_both),
		cmocka_unit_test(test_quat_from_matrix4_same_address),
		cmocka_unit_test(test_quat_from_matrix4_same_address_negative_trace),
		cmocka_unit_test(test_quat_from_matrix3_same_address),
		cmocka_unit_test(test_vec3_rotate_dst_aliases_matrix_row),
		cmocka_unit_test(test_vec3_transform3x4_dst_aliases_matrix_row),
		cmocka_unit_test(test_matrix3_scale_vec_aliases_row),
		cmocka_unit_test(test_matrix3_mirrorv_vec_aliases_row),
		cmocka_unit_test(test_matrix3_mirror_plane_same_address),
		cmocka_unit_test(test_plane_from_tri_v1_same_address),
		cmocka_unit_test(test_bounds_move_vec_aliases_min),
		cmocka_unit_test(test_bounds_scale_vec_aliases_min),
		cmocka_unit_test(test_bounds_merge_point_vec_aliases_min),
		cmocka_unit_test(test_bounds_get_center_dst_aliases_min),
		cmocka_unit_test(test_calc_torque_dst_aliases_v1),
		cmocka_unit_test(test_polar_to_norm_same_address),
		cmocka_unit_test(test_gs_u8x4_to_float4_same_base),
	};

	return cmocka_run_group_tests(tests, NULL, NULL);
}
