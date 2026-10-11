#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <cmocka.h>

#include <math.h>

#include <util/c99defs.h>
#include <graphics/axisang.h>
#include <graphics/matrix3.h>
#include <graphics/matrix4.h>
#include <graphics/quat.h>
#include <graphics/vec3.h>

/* The header-inline setters in graphics/quat.h, and the exported
 * functions in graphics/quat.c. The .c cases pin the current C behavior. */

static bool close_enough(float a, float b)
{
	return fabsf(a - b) < 1e-5f;
}

static bool quat_eq(const struct quat *q, float x, float y, float z, float w)
{
	return q->x == x && q->y == y && q->z == z && q->w == w;
}

static void test_quat_set_stores_xyzw_in_order(void **state)
{
	UNUSED_PARAMETER(state);

	struct quat q;

	quat_set(&q, 1.0f, 2.0f, 3.0f, 4.0f);
	assert_true(q.x == 1.0f);
	assert_true(q.y == 2.0f);
	assert_true(q.z == 3.0f);
	assert_true(q.w == 4.0f);

	/* ptr[] aliases the same lanes */
	assert_true(q.ptr[0] == 1.0f);
	assert_true(q.ptr[3] == 4.0f);
}

static void test_quat_set_matches_quat_identity(void **state)
{
	UNUSED_PARAMETER(state);

	struct quat a, b;

	quat_identity(&a);
	quat_set(&b, 0.0f, 0.0f, 0.0f, 1.0f);
	assert_true(a.x == b.x && a.y == b.y && a.z == b.z && a.w == b.w);
}

/* A zero angle is the identity, exactly: sin(0) == 0, cos(0) == 1. */
static void test_quat_from_axisang_zero_is_identity(void **state)
{
	UNUSED_PARAMETER(state);

	struct axisang aa;
	struct quat q;

	axisang_set(&aa, 0.0f, 1.0f, 0.0f, 0.0f);
	quat_from_axisang(&q, &aa);
	assert_true(quat_eq(&q, 0.0f, 0.0f, 0.0f, 1.0f));
}

/* 90 degrees about Z. */
static void test_quat_from_axisang_z90(void **state)
{
	UNUSED_PARAMETER(state);

	struct axisang aa;
	struct quat q;
	const float half = 0.70710678f;

	axisang_set(&aa, 0.0f, 0.0f, 1.0f, 3.14159265f / 2.0f);
	quat_from_axisang(&q, &aa);
	assert_true(close_enough(q.x, 0.0f) && close_enough(q.y, 0.0f));
	assert_true(close_enough(q.z, half) && close_enough(q.w, half));
}

static void test_quat_mul_identity(void **state)
{
	UNUSED_PARAMETER(state);

	struct quat a, b, d;

	quat_identity(&a);
	quat_identity(&b);
	quat_mul(&d, &a, &b);
	assert_true(quat_eq(&d, 0.0f, 0.0f, 0.0f, 1.0f));
}

static void test_quat_from_matrix4_identity(void **state)
{
	UNUSED_PARAMETER(state);

	struct matrix4 m;
	struct quat q;

	matrix4_identity(&m);
	quat_from_matrix4(&q, &m);
	assert_true(quat_eq(&q, 0.0f, 0.0f, 0.0f, 1.0f));
}

/* 180 degrees about X: trace is negative, so the else branch runs. */
static void test_quat_from_matrix4_x180(void **state)
{
	UNUSED_PARAMETER(state);

	struct matrix4 m;
	struct quat q;

	matrix4_identity(&m);
	m.y.y = -1.0f;
	m.z.z = -1.0f;
	quat_from_matrix4(&q, &m);
	assert_true(quat_eq(&q, 1.0f, 0.0f, 0.0f, 0.0f));
}

static void test_quat_from_matrix3_identity(void **state)
{
	UNUSED_PARAMETER(state);

	struct matrix3 m;
	struct quat q;

	matrix3_identity(&m);
	quat_from_matrix3(&q, &m);
	assert_true(quat_eq(&q, 0.0f, 0.0f, 0.0f, 1.0f));
}

/* Identity looks along -Z in this function (it negates the direction). */
static void test_quat_get_dir_identity_is_pos_z(void **state)
{
	UNUSED_PARAMETER(state);

	struct quat q;
	struct vec3 d;

	quat_identity(&q);
	quat_get_dir(&d, &q);
	assert_true(d.x == 0.0f && d.y == 0.0f && d.z == 1.0f && d.w == 0.0f);
}

static void test_quat_set_look_dir_neg_z_is_identity(void **state)
{
	UNUSED_PARAMETER(state);

	struct vec3 dir;
	struct quat q;

	vec3_set(&dir, 0.0f, 0.0f, -1.0f);
	quat_set_look_dir(&q, &dir);
	assert_true(quat_eq(&q, 0.0f, 0.0f, 0.0f, 1.0f));
}

static void test_quat_log_identity_is_zero(void **state)
{
	UNUSED_PARAMETER(state);

	struct quat q, d;

	quat_identity(&q);
	quat_log(&d, &q);
	assert_true(quat_eq(&d, 0.0f, 0.0f, 0.0f, 0.0f));
}

static void test_quat_exp_zero_is_identity(void **state)
{
	UNUSED_PARAMETER(state);

	struct quat q, d;

	quat_set(&q, 0.0f, 0.0f, 0.0f, 0.0f);
	quat_exp(&d, &q);
	assert_true(quat_eq(&d, 0.0f, 0.0f, 0.0f, 1.0f));
}

static void test_quat_interpolate_identical_is_input(void **state)
{
	UNUSED_PARAMETER(state);

	struct quat q, d;

	quat_set(&q, 0.1f, -0.2f, 0.3f, 0.4f);
	quat_interpolate(&d, &q, &q, 0.0f);
	assert_true(quat_eq(&d, 0.1f, -0.2f, 0.3f, 0.4f));
	quat_interpolate(&d, &q, &q, 1.0f);
	assert_true(quat_eq(&d, 0.1f, -0.2f, 0.3f, 0.4f));
}

static void test_quat_get_tangent_equal_inputs_is_zero(void **state)
{
	UNUSED_PARAMETER(state);

	struct quat q, d;

	quat_set(&q, 0.2f, 0.3f, -0.4f, 0.5f);
	quat_get_tangent(&d, &q, &q, &q);
	assert_true(quat_eq(&d, 0.0f, 0.0f, 0.0f, 0.0f));
}

static void test_quat_interpolate_cubic_t0_is_q1(void **state)
{
	UNUSED_PARAMETER(state);

	struct quat q1, q2, m1, m2, d;

	quat_set(&q1, 0.0f, 0.0f, 0.0f, 1.0f);
	quat_set(&q2, 1.0f, 0.0f, 0.0f, 0.0f);
	quat_set(&m1, 0.0f, 1.0f, 0.0f, 0.0f);
	quat_set(&m2, 0.0f, 0.0f, 1.0f, 0.0f);
	quat_interpolate_cubic(&d, &q1, &q2, &m1, &m2, 0.0f);
	assert_true(quat_eq(&d, q1.x, q1.y, q1.z, q1.w));
}

int main(void)
{
	const struct CMUnitTest tests[] = {
		cmocka_unit_test(test_quat_set_stores_xyzw_in_order),
		cmocka_unit_test(test_quat_set_matches_quat_identity),
		cmocka_unit_test(test_quat_from_axisang_zero_is_identity),
		cmocka_unit_test(test_quat_from_axisang_z90),
		cmocka_unit_test(test_quat_mul_identity),
		cmocka_unit_test(test_quat_from_matrix4_identity),
		cmocka_unit_test(test_quat_from_matrix4_x180),
		cmocka_unit_test(test_quat_from_matrix3_identity),
		cmocka_unit_test(test_quat_get_dir_identity_is_pos_z),
		cmocka_unit_test(test_quat_set_look_dir_neg_z_is_identity),
		cmocka_unit_test(test_quat_log_identity_is_zero),
		cmocka_unit_test(test_quat_exp_zero_is_identity),
		cmocka_unit_test(test_quat_interpolate_identical_is_input),
		cmocka_unit_test(test_quat_get_tangent_equal_inputs_is_zero),
		cmocka_unit_test(test_quat_interpolate_cubic_t0_is_q1),
	};

	return cmocka_run_group_tests(tests, NULL, NULL);
}
