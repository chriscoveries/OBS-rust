#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <setjmp.h>
#include <string.h>
#include <cmocka.h>

#include <util/c99defs.h>
#include <graphics/math-extra.h>
#include <graphics/matrix3.h>
#include <graphics/matrix4.h>
#include <graphics/plane.h>
#include <graphics/quat.h>
#include <graphics/srgb.h>
#include <graphics/vec2.h>
#include <graphics/vec3.h>

/* The arrangements left after test_graphics_alias_ordinary. Each test runs
 * the call on separate storage and on the overlap the 2026-10-12 audit
 * measured, and requires the same result. get_3x3_submatrix is static in
 * matrix4.c, so it has no entry here. */

static void assert_vec3_same(const struct vec3 *a, const struct vec3 *b)
{
	assert_true(a->x == b->x);
	assert_true(a->y == b->y);
	assert_true(a->z == b->z);
	assert_true(a->w == b->w);
}

static void assert_quat_same(const struct quat *a, const struct quat *b)
{
	assert_true(a->x == b->x);
	assert_true(a->y == b->y);
	assert_true(a->z == b->z);
	assert_true(a->w == b->w);
}

static void assert_matrix3_same(const struct matrix3 *a, const struct matrix3 *b)
{
	assert_vec3_same(&a->x, &b->x);
	assert_vec3_same(&a->y, &b->y);
	assert_vec3_same(&a->z, &b->z);
	assert_vec3_same(&a->t, &b->t);
}

static void set_v3(struct vec3 *v, float x, float y, float z)
{
	vec3_set(v, x, y, z);
	v->w = 0.0f;
}

static void set_v4(struct vec4 *v, float x, float y, float z, float w)
{
	v->x = x;
	v->y = y;
	v->z = z;
	v->w = w;
}

/* 90 degree Z rotation. The positive-trace path writes dst->y into the
 * matrix's x.y lane, then reads that lane for dst->z. */
static void set_rot_z(struct matrix4 *m)
{
	set_v4(&m->x, 0.0f, -1.0f, 0.0f, 0.0f);
	set_v4(&m->y, 1.0f, 0.0f, 0.0f, 0.0f);
	set_v4(&m->z, 0.0f, 0.0f, 1.0f, 0.0f);
	set_v4(&m->t, 0.0f, 0.0f, 0.0f, 1.0f);
}

static void test_quat_mul_dst_aliases_q1(void **state)
{
	struct quat q, out, alias;

	UNUSED_PARAMETER(state);
	quat_set(&q, 0.2f, -0.3f, 0.4f, 0.8f);
	quat_mul(&out, &q, &q);

	quat_copy(&alias, &q);
	quat_mul(&alias, &alias, &q);
	assert_quat_same(&alias, &out);
}

static void test_quat_from_matrix4_same_address(void **state)
{
	struct matrix4 src, over;
	struct quat out;

	UNUSED_PARAMETER(state);
	set_rot_z(&src);
	quat_from_matrix4(&out, &src);

	over = src;
	quat_from_matrix4((struct quat *)&over, &over);
	assert_quat_same((struct quat *)&over, &out);
}

static void test_quat_from_matrix3_same_address(void **state)
{
	struct matrix3 src, over;
	struct quat out;

	UNUSED_PARAMETER(state);
	matrix3_identity(&src);
	set_v3(&src.x, 0.0f, -1.0f, 0.0f);
	set_v3(&src.y, 1.0f, 0.0f, 0.0f);
	set_v3(&src.z, 0.0f, 0.0f, 1.0f);
	quat_from_matrix3(&out, &src);

	over = src;
	quat_from_matrix3((struct quat *)&over, &over);
	assert_quat_same((struct quat *)&over, &out);
}

static void test_vec3_rotate_dst_aliases_y(void **state)
{
	struct matrix3 src, over;
	struct vec3 v, out;

	UNUSED_PARAMETER(state);
	matrix3_identity(&src);
	set_v3(&v, 1.0f, 2.0f, 3.0f);
	vec3_rotate(&out, &v, &src);

	over = src;
	vec3_rotate(&over.y, &v, &over);
	assert_vec3_same(&over.y, &out);
}

static void test_vec3_transform3x4_dst_aliases_y(void **state)
{
	struct matrix3 src, over;
	struct vec3 v, out;

	UNUSED_PARAMETER(state);
	matrix3_identity(&src);
	set_v3(&v, 1.0f, 2.0f, 3.0f);
	vec3_transform3x4(&out, &v, &src);

	over = src;
	vec3_transform3x4(&over.y, &v, &over);
	assert_vec3_same(&over.y, &out);
}

static void test_matrix3_scale_v_aliases_x(void **state)
{
	struct matrix3 src, out, over;

	UNUSED_PARAMETER(state);
	matrix3_identity(&src);
	set_v3(&src.x, 2.0f, 3.0f, 4.0f);
	set_v3(&src.y, 1.0f, 0.0f, 0.0f);
	set_v3(&src.z, 0.0f, 1.0f, 0.0f);
	set_v3(&src.t, 5.0f, 6.0f, 7.0f);

	matrix3_scale(&out, &src, &src.x);

	over = src;
	matrix3_scale(&over, &src, &over.x);
	assert_matrix3_same(&over, &out);
}

static void test_matrix3_mirrorv_v_aliases_x(void **state)
{
	struct matrix3 src, out, over;
	struct vec3 dir;

	UNUSED_PARAMETER(state);
	matrix3_identity(&src);
	set_v3(&src.y, 0.0f, 0.0f, 1.0f);
	set_v3(&src.t, 2.0f, 3.0f, 4.0f);
	set_v3(&dir, 0.0f, 0.0f, 1.0f);

	matrix3_mirrorv(&out, &src, &dir);

	over = src;
	set_v3(&over.x, 0.0f, 0.0f, 1.0f);
	matrix3_mirrorv(&over, &src, &over.x);
	assert_matrix3_same(&over, &out);
}

/* plane.dir overlays the destination's x row. */
static void test_matrix3_mirror_dir_aliases_x(void **state)
{
	struct matrix3 src, out;
	struct plane separate;
	union {
		struct matrix3 m;
		struct plane p;
	} over;

	UNUSED_PARAMETER(state);
	matrix3_identity(&src);
	set_v3(&src.y, 0.0f, 0.0f, 1.0f);
	set_v3(&separate.dir, 0.0f, 0.0f, 1.0f);
	separate.dist = 0.0f;
	matrix3_mirror(&out, &src, &separate);

	matrix3_identity(&over.m);
	set_v3(&over.m.x, 0.0f, 0.0f, 1.0f);
	over.m.y.x = 0.0f;
	matrix3_mirror(&over.m, &src, &over.p);
	assert_matrix3_same(&over.m, &out);
}

static void test_calc_torque_overlap_matches_separate(void **state)
{
	struct vec3 v1, v2, out, over;

	UNUSED_PARAMETER(state);
	set_v3(&v1, 10.0f, 0.0f, 0.0f);
	v1.w = 9.0f;
	set_v3(&v2, 0.0f, 0.0f, 0.0f);
	v2.w = 4.0f;

	calc_torque(&out, &v1, &v2, 0.5f, 0.1f, 1.0f);

	over = v1;
	calc_torque(&over, &over, &v2, 0.5f, 0.1f, 1.0f);
	assert_vec3_same(&over, &out);
}

static void test_polar_to_norm_same_address(void **state)
{
	struct vec2 polar;
	struct vec3 out, over;

	UNUSED_PARAMETER(state);
	polar.x = 0.5f;
	polar.y = 0.25f;
	set_v3(&out, 9.0f, 9.0f, 9.0f);
	out.w = 7.0f;
	polar_to_norm(&out, &polar);

	over.x = 0.5f;
	over.y = 0.25f;
	over.z = 9.0f;
	over.w = 7.0f;
	polar_to_norm(&over, (struct vec2 *)&over);
	assert_vec3_same(&over, &out);
}

static void test_gs_u8x4_to_float4_same_base(void **state)
{
	uint8_t bytes[4] = {10, 20, 30, 40};
	float separate[4];
	union {
		float f[4];
		uint8_t u[16];
	} over;

	UNUSED_PARAMETER(state);
	gs_u8x4_to_float4(separate, bytes);

	memset(&over, 0, sizeof(over));
	over.u[0] = 10;
	over.u[1] = 20;
	over.u[2] = 30;
	over.u[3] = 40;
	gs_u8x4_to_float4(over.f, over.u);

	assert_true(over.f[0] == separate[0]);
	assert_true(over.f[1] == separate[1]);
	assert_true(over.f[2] == separate[2]);
	assert_true(over.f[3] == separate[3]);
}

int main(void)
{
	const struct CMUnitTest tests[] = {
		cmocka_unit_test(test_quat_mul_dst_aliases_q1),
		cmocka_unit_test(test_quat_from_matrix4_same_address),
		cmocka_unit_test(test_quat_from_matrix3_same_address),
		cmocka_unit_test(test_vec3_rotate_dst_aliases_y),
		cmocka_unit_test(test_vec3_transform3x4_dst_aliases_y),
		cmocka_unit_test(test_matrix3_scale_v_aliases_x),
		cmocka_unit_test(test_matrix3_mirrorv_v_aliases_x),
		cmocka_unit_test(test_matrix3_mirror_dir_aliases_x),
		cmocka_unit_test(test_calc_torque_overlap_matches_separate),
		cmocka_unit_test(test_polar_to_norm_same_address),
		cmocka_unit_test(test_gs_u8x4_to_float4_same_base),
	};

	return cmocka_run_group_tests(tests, NULL, NULL);
}
