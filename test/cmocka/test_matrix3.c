#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <cmocka.h>

#include <util/c99defs.h>
#include <graphics/axisang.h>
#include <graphics/matrix3.h>
#include <graphics/matrix4.h>
#include <graphics/plane.h>
#include <graphics/quat.h>

/* Characterization of the exported functions in graphics/matrix3.c.
 * Odd lane and failure behavior is characterized, not endorsed. */

static void set_row(struct vec3 *v, float x, float y, float z, float w)
{
	vec3_set(v, x, y, z);
	v->w = w;
}

static void assert_row(const struct vec3 *v, float x, float y, float z, float w)
{
	assert_true(v->x == x && v->y == y && v->z == z && v->w == w);
}

static void assert_identity_axes(const struct matrix3 *m)
{
	assert_row(&m->x, 1.0f, 0.0f, 0.0f, 0.0f);
	assert_row(&m->y, 0.0f, 1.0f, 0.0f, 0.0f);
	assert_row(&m->z, 0.0f, 0.0f, 1.0f, 0.0f);
}

static void assert_identity(const struct matrix3 *m)
{
	assert_identity_axes(m);
	assert_row(&m->t, 0.0f, 0.0f, 0.0f, 0.0f);
}

static void fill_matrix(struct matrix3 *m, float v)
{
	float *p = (float *)m;
	int i;

	for (i = 0; i < 16; i++)
		p[i] = v;
}

/* A zero quaternion has norm 0, so the scale is 0 and the result is identity. */
static void test_matrix3_from_quat_zero_is_identity(void **state)
{
	struct quat q;
	struct matrix3 m;

	UNUSED_PARAMETER(state);
	quat_set(&q, 0.0f, 0.0f, 0.0f, 0.0f);
	matrix3_from_quat(&m, &q);
	assert_identity(&m);
}

static void test_matrix3_from_quat_identity(void **state)
{
	struct quat q;
	struct matrix3 m;

	UNUSED_PARAMETER(state);
	quat_identity(&q);
	matrix3_from_quat(&m, &q);
	assert_identity(&m);

	/* A non-unit scalar quaternion normalizes to the same matrix. */
	quat_set(&q, 0.0f, 0.0f, 0.0f, 2.0f);
	matrix3_from_quat(&m, &q);
	assert_identity(&m);
}

/* 180 degrees about x: quat (1, 0, 0, 0). */
static void test_matrix3_from_quat_x180(void **state)
{
	struct quat q;
	struct matrix3 m;

	UNUSED_PARAMETER(state);
	quat_set(&q, 1.0f, 0.0f, 0.0f, 0.0f);
	matrix3_from_quat(&m, &q);
	assert_row(&m.x, 1.0f, 0.0f, 0.0f, 0.0f);
	assert_row(&m.y, 0.0f, -1.0f, 0.0f, 0.0f);
	assert_row(&m.z, 0.0f, 0.0f, -1.0f, 0.0f);
	assert_row(&m.t, 0.0f, 0.0f, 0.0f, 0.0f);
}

/* Angle 0 yields the identity quaternion, then the identity matrix. */
static void test_matrix3_from_axisang_zero(void **state)
{
	struct axisang aa;
	struct matrix3 m;

	UNUSED_PARAMETER(state);
	axisang_set(&aa, 0.0f, 0.0f, 1.0f, 0.0f);
	matrix3_from_axisang(&m, &aa);
	assert_identity(&m);
}

/* Copies the four rows, then forces every w lane to 0. */
static void test_matrix3_from_matrix4_clears_w(void **state)
{
	struct matrix4 src;
	struct matrix3 m;

	UNUSED_PARAMETER(state);
	vec4_set(&src.x, 1.0f, 2.0f, 3.0f, 4.0f);
	vec4_set(&src.y, 5.0f, 6.0f, 7.0f, 8.0f);
	vec4_set(&src.z, 9.0f, 10.0f, 11.0f, 12.0f);
	vec4_set(&src.t, 13.0f, 14.0f, 15.0f, 16.0f);
	matrix3_from_matrix4(&m, &src);
	assert_row(&m.x, 1.0f, 2.0f, 3.0f, 0.0f);
	assert_row(&m.y, 5.0f, 6.0f, 7.0f, 0.0f);
	assert_row(&m.z, 9.0f, 10.0f, 11.0f, 0.0f);
	assert_row(&m.t, 13.0f, 14.0f, 15.0f, 0.0f);
}

static void test_matrix3_mul_scale_and_alias(void **state)
{
	struct matrix3 m1, m2, dst, aliased;

	UNUSED_PARAMETER(state);
	matrix3_identity(&m1);
	set_row(&m1.t, 1.0f, 2.0f, 3.0f, 0.0f);
	matrix3_identity(&m2);
	m2.x.x = 2.0f;
	m2.y.y = 3.0f;
	m2.z.z = 4.0f;

	matrix3_mul(&dst, &m1, &m2);
	assert_row(&dst.x, 2.0f, 0.0f, 0.0f, 0.0f);
	assert_row(&dst.y, 0.0f, 3.0f, 0.0f, 0.0f);
	assert_row(&dst.z, 0.0f, 0.0f, 4.0f, 0.0f);
	assert_row(&dst.t, 2.0f, 6.0f, 12.0f, 0.0f);

	/* dst == m2 is computed through a temporary, so m2 is not half-updated. */
	aliased = m2;
	matrix3_mul(&aliased, &m1, &aliased);
	assert_row(&aliased.x, 2.0f, 0.0f, 0.0f, 0.0f);
	assert_row(&aliased.y, 0.0f, 3.0f, 0.0f, 0.0f);
	assert_row(&aliased.z, 0.0f, 0.0f, 4.0f, 0.0f);
	assert_row(&aliased.t, 2.0f, 6.0f, 12.0f, 0.0f);
}

static void test_matrix3_rotate_x180(void **state)
{
	struct matrix3 m, dst;
	struct quat q;

	UNUSED_PARAMETER(state);
	matrix3_identity(&m);
	set_row(&m.t, 1.0f, 2.0f, 3.0f, 0.0f);
	quat_set(&q, 1.0f, 0.0f, 0.0f, 0.0f);
	matrix3_rotate(&dst, &m, &q);
	assert_row(&dst.x, 1.0f, 0.0f, 0.0f, 0.0f);
	assert_row(&dst.y, 0.0f, -1.0f, 0.0f, 0.0f);
	assert_row(&dst.z, 0.0f, 0.0f, -1.0f, 0.0f);
	assert_row(&dst.t, 1.0f, -2.0f, -3.0f, 0.0f);
}

static void test_matrix3_rotate_aa_zero(void **state)
{
	struct matrix3 m, dst;
	struct axisang aa;

	UNUSED_PARAMETER(state);
	matrix3_identity(&m);
	set_row(&m.t, 4.0f, 5.0f, 6.0f, 9.0f);
	axisang_set(&aa, 0.0f, 1.0f, 0.0f, 0.0f);
	matrix3_rotate_aa(&dst, &m, &aa);
	assert_identity_axes(&dst);
	assert_row(&dst.t, 4.0f, 5.0f, 6.0f, 0.0f);
}

/* vec3_mul multiplies every lane, including w. It does not clear w. */
static void test_matrix3_scale_keeps_w(void **state)
{
	struct matrix3 m, dst;
	struct vec3 v;

	UNUSED_PARAMETER(state);
	matrix3_identity(&m);
	m.x.w = 2.0f;
	m.y.w = 4.0f;
	m.z.w = 6.0f;
	m.t.w = 8.0f;
	vec3_set(&v, 2.0f, 3.0f, 4.0f);
	v.w = 5.0f;
	matrix3_scale(&dst, &m, &v);
	assert_row(&dst.x, 2.0f, 0.0f, 0.0f, 10.0f);
	assert_row(&dst.y, 0.0f, 3.0f, 0.0f, 20.0f);
	assert_row(&dst.z, 0.0f, 0.0f, 4.0f, 30.0f);
	assert_row(&dst.t, 0.0f, 0.0f, 0.0f, 40.0f);
}

/* The SSE transpose writes z.w into the w lane of every axis row.
 * x.w and y.w of the source are discarded. Translation is rotated, then
 * negated, and that negation clears w. Characterized, not endorsed. */
static void test_matrix3_transpose_broadcasts_zw(void **state)
{
	struct matrix3 m, dst;

	UNUSED_PARAMETER(state);
	matrix3_identity(&m);
	m.x.w = 3.0f;
	m.y.w = 4.0f;
	m.z.w = 7.0f;
	set_row(&m.t, 1.0f, 2.0f, 3.0f, 0.0f);
	matrix3_transpose(&dst, &m);
	assert_row(&dst.x, 1.0f, 0.0f, 0.0f, 7.0f);
	assert_row(&dst.y, 0.0f, 1.0f, 0.0f, 7.0f);
	assert_row(&dst.z, 0.0f, 0.0f, 1.0f, 7.0f);
	assert_row(&dst.t, -1.0f, -2.0f, -3.0f, 0.0f);
}

static void test_matrix3_inv_scale_and_translation(void **state)
{
	struct matrix3 m, dst;

	UNUSED_PARAMETER(state);
	matrix3_identity(&m);
	m.x.x = 2.0f;
	m.y.y = 4.0f;
	m.z.z = 8.0f;
	matrix3_inv(&dst, &m);
	assert_row(&dst.x, 0.5f, 0.0f, 0.0f, 0.0f);
	assert_row(&dst.y, 0.0f, 0.25f, 0.0f, 0.0f);
	assert_row(&dst.z, 0.0f, 0.0f, 0.125f, 0.0f);
	assert_row(&dst.t, 0.0f, 0.0f, 0.0f, 0.0f);

	matrix3_identity(&m);
	set_row(&m.t, 4.0f, -2.0f, 8.0f, 5.0f);
	matrix3_inv(&dst, &m);
	assert_identity_axes(&dst);
	assert_row(&dst.t, -4.0f, 2.0f, -8.0f, 0.0f);

	/* 0.125^3 is above the 0.0005 determinant cutoff. */
	matrix3_identity(&m);
	m.x.x = m.y.y = m.z.z = 0.125f;
	matrix3_inv(&dst, &m);
	assert_row(&dst.x, 8.0f, 0.0f, 0.0f, 0.0f);
	assert_row(&dst.y, 0.0f, 8.0f, 0.0f, 0.0f);
	assert_row(&dst.z, 0.0f, 0.0f, 8.0f, 0.0f);
	assert_row(&dst.t, 0.0f, 0.0f, 0.0f, 0.0f);
}

/* Below the cutoff, matrix4_inv writes nothing and matrix3_inv only clears
 * t.w. Characterized, not endorsed. */
static void test_matrix3_inv_singular_keeps_dst(void **state)
{
	struct matrix3 m, dst;
	const float *p;
	int i;

	UNUSED_PARAMETER(state);
	matrix3_identity(&m);
	m.x.x = m.y.y = m.z.z = 0.0625f;
	fill_matrix(&dst, 9.0f);
	matrix3_inv(&dst, &m);
	p = (const float *)&dst;
	for (i = 0; i < 15; i++)
		assert_true(p[i] == 9.0f);
	assert_true(dst.t.w == 0.0f);

	/* Aliased singular input: axes stay, and t.w is forced to 0. */
	fill_matrix(&m, 0.0f);
	set_row(&m.t, 1.0f, 2.0f, 3.0f, 5.0f);
	matrix3_inv(&m, &m);
	assert_row(&m.x, 0.0f, 0.0f, 0.0f, 0.0f);
	assert_row(&m.y, 0.0f, 0.0f, 0.0f, 0.0f);
	assert_row(&m.z, 0.0f, 0.0f, 0.0f, 0.0f);
	assert_row(&m.t, 1.0f, 2.0f, 3.0f, 0.0f);
}

static void test_matrix3_mirror_and_mirrorv(void **state)
{
	struct matrix3 m, dst;
	struct plane p;
	struct vec3 n;

	UNUSED_PARAMETER(state);
	matrix3_identity(&m);
	set_row(&m.t, 1.0f, 2.0f, 3.0f, 0.0f);
	vec3_set(&n, 1.0f, 0.0f, 0.0f);
	plane_set(&p, &n, 0.0f);
	matrix3_mirror(&dst, &m, &p);
	assert_row(&dst.x, -1.0f, 0.0f, 0.0f, 0.0f);
	assert_row(&dst.y, 0.0f, 1.0f, 0.0f, 0.0f);
	assert_row(&dst.z, 0.0f, 0.0f, 1.0f, 0.0f);
	assert_row(&dst.t, -1.0f, 2.0f, 3.0f, 0.0f);

	matrix3_identity(&m);
	set_row(&m.t, 1.0f, 2.0f, 3.0f, 0.0f);
	vec3_set(&n, 0.0f, 1.0f, 0.0f);
	matrix3_mirrorv(&dst, &m, &n);
	assert_row(&dst.x, 1.0f, 0.0f, 0.0f, 0.0f);
	assert_row(&dst.y, 0.0f, -1.0f, 0.0f, 0.0f);
	assert_row(&dst.z, 0.0f, 0.0f, 1.0f, 0.0f);
	assert_row(&dst.t, 1.0f, -2.0f, 3.0f, 0.0f);

	/* vec3_mirrorv's dot includes both w lanes. Characterized, not endorsed. */
	matrix3_identity(&m);
	m.x.w = 2.0f;
	vec3_set(&n, 1.0f, 0.0f, 0.0f);
	n.w = 3.0f;
	matrix3_mirrorv(&dst, &m, &n);
	assert_row(&dst.x, -13.0f, 0.0f, 0.0f, 0.0f);
	assert_row(&dst.y, 0.0f, 1.0f, 0.0f, 0.0f);
	assert_row(&dst.z, 0.0f, 0.0f, 1.0f, 0.0f);
	assert_row(&dst.t, 0.0f, 0.0f, 0.0f, 0.0f);
}

int main(void)
{
	const struct CMUnitTest tests[] = {
		cmocka_unit_test(test_matrix3_from_quat_zero_is_identity),
		cmocka_unit_test(test_matrix3_from_quat_identity),
		cmocka_unit_test(test_matrix3_from_quat_x180),
		cmocka_unit_test(test_matrix3_from_axisang_zero),
		cmocka_unit_test(test_matrix3_from_matrix4_clears_w),
		cmocka_unit_test(test_matrix3_mul_scale_and_alias),
		cmocka_unit_test(test_matrix3_rotate_x180),
		cmocka_unit_test(test_matrix3_rotate_aa_zero),
		cmocka_unit_test(test_matrix3_scale_keeps_w),
		cmocka_unit_test(test_matrix3_transpose_broadcasts_zw),
		cmocka_unit_test(test_matrix3_inv_scale_and_translation),
		cmocka_unit_test(test_matrix3_inv_singular_keeps_dst),
		cmocka_unit_test(test_matrix3_mirror_and_mirrorv),
	};

	return cmocka_run_group_tests(tests, NULL, NULL);
}
