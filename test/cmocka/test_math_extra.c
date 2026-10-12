#include <math.h>
#include <stdlib.h>
#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <cmocka.h>

#include <graphics/math-extra.h>
#include <graphics/vec2.h>
#include <graphics/vec3.h>

/* Characterization of the exported functions in graphics/math-extra.c.
 * Odd lane and comparison behavior is characterized, not endorsed.
 * Header-inline get_percentage and get_percentagei stay out of this file.
 *
 * polar_to_cart, and cart_to_polar when the length is outside EPSILON,
 * copy an uninitialized w. Those tests do not read that lane. */

static void set_vec(struct vec3 *v, float x, float y, float z, float w)
{
	vec3_set(v, x, y, z);
	v->w = w;
}

static void assert_xyz(const struct vec3 *v, float x, float y, float z)
{
	assert_true(v->x == x && v->y == y && v->z == z);
}

static void assert_vec(const struct vec3 *v, float x, float y, float z, float w)
{
	assert_xyz(v, x, y, z);
	assert_true(v->w == w);
}

/* (pitch 0, yaw 0, radius r) is +Z. cos(0) and sin(0) are exact. */
static void test_polar_to_cart_plus_z(void **state)
{
	struct vec3 polar, cart;

	UNUSED_PARAMETER(state);
	set_vec(&polar, 0.0f, 0.0f, 2.0f, 0.0f);
	polar_to_cart(&cart, &polar);
	assert_xyz(&cart, 0.0f, 0.0f, 2.0f);
}

/* A zero radius is a zero vector even when the angles are not. */
static void test_polar_to_cart_zero_radius(void **state)
{
	struct vec3 polar, cart;

	UNUSED_PARAMETER(state);
	set_vec(&polar, 1.0f, 2.0f, 0.0f, 9.0f);
	polar_to_cart(&cart, &polar);
	assert_xyz(&cart, 0.0f, 0.0f, 0.0f);
}

/* The conversion uses a local, then copies, so dst may alias the input. */
static void test_polar_to_cart_alias(void **state)
{
	struct vec3 v;

	UNUSED_PARAMETER(state);
	set_vec(&v, 0.0f, 0.0f, 2.0f, 0.0f);
	polar_to_cart(&v, &v);
	assert_xyz(&v, 0.0f, 0.0f, 2.0f);
}

/* Length within EPSILON becomes all zeros, including w. */
static void test_cart_to_polar_within_epsilon(void **state)
{
	struct vec3 cart, polar;

	UNUSED_PARAMETER(state);
	set_vec(&cart, 0.0f, 0.0f, 0.0f, 1e-4f);
	cart_to_polar(&polar, &cart);
	assert_vec(&polar, 0.0f, 0.0f, 0.0f, 0.0f);

	set_vec(&cart, 1e-4f, 0.0f, 0.0f, 0.0f);
	cart_to_polar(&polar, &cart);
	assert_vec(&polar, 0.0f, 0.0f, 0.0f, 0.0f);
}

/* +Z of length 2 is (pitch 0, yaw 0, radius 2). w is not written. */
static void test_cart_to_polar_plus_z(void **state)
{
	struct vec3 cart, polar;

	UNUSED_PARAMETER(state);
	set_vec(&cart, 0.0f, 0.0f, 2.0f, 0.0f);
	cart_to_polar(&polar, &cart);
	assert_xyz(&polar, 0.0f, 0.0f, 2.0f);
}

/* vec3_len dots w, so a pure w lane is a radius. atan2(0, 0) is the yaw. */
static void test_cart_to_polar_w_lane(void **state)
{
	struct vec3 cart, polar;

	UNUSED_PARAMETER(state);
	set_vec(&cart, 0.0f, 0.0f, 0.0f, 2e-4f);
	cart_to_polar(&polar, &cart);
	assert_xyz(&polar, 0.0f, 0.0f, 2e-4f);
}

/* +Z normal is (yaw 0, pitch 0). polar_to_norm leaves dst->w alone. */
static void test_norm_polar_roundtrip_plus_z(void **state)
{
	struct vec3 norm, back;
	struct vec2 polar;

	UNUSED_PARAMETER(state);
	set_vec(&norm, 0.0f, 0.0f, 1.0f, 4.0f);
	norm_to_polar(&polar, &norm);
	assert_true(polar.x == 0.0f && polar.y == 0.0f);

	set_vec(&back, 9.0f, 9.0f, 9.0f, 7.0f);
	polar_to_norm(&back, &polar);
	assert_vec(&back, 0.0f, 0.0f, 1.0f, 7.0f);
}

static void test_calc_torquef_close_returns_target(void **state)
{
	UNUSED_PARAMETER(state);
	/* |1 - 1.00005| == 5e-5, which is within EPSILON (1e-4). */
	assert_true(calc_torquef(1.0f, 1.00005f, 1.0f, 0.1f, 1.0f) == 1.00005f);
}

static void test_calc_torquef_steps_and_clamps(void **state)
{
	UNUSED_PARAMETER(state);
	assert_true(calc_torquef(0.0f, 10.0f, 0.5f, 0.1f, 1.0f) == 5.0f);
	/* dist 0.01 is raised to min_adjust. */
	assert_true(calc_torquef(0.0f, 10.0f, 0.001f, 0.5f, 1.0f) == 0.5f);
	/* Overshoot clamps to the target. */
	assert_true(calc_torquef(0.0f, 1.0f, 1.0f, 0.1f, 10.0f) == 1.0f);

	assert_true(calc_torquef(10.0f, 0.0f, 0.5f, 0.1f, 1.0f) == 5.0f);
	assert_true(calc_torquef(10.0f, 0.0f, 0.001f, 0.5f, 1.0f) == 9.5f);
	assert_true(calc_torquef(1.0f, 0.0f, 1.0f, 0.1f, 10.0f) == 0.0f);
}

/* vec3_close is v1 - v2 < epsilon on each lane, not an absolute test.
 * v2 at (4, 2, 8) still counts as close to the origin, so the result is v2,
 * including w. Characterized, not endorsed. */
static void test_calc_torque_one_sided_close_copies_target(void **state)
{
	struct vec3 v1, v2, dst;

	UNUSED_PARAMETER(state);
	set_vec(&v1, 0.0f, 0.0f, 0.0f, 1.0f);
	set_vec(&v2, 4.0f, 2.0f, 8.0f, 5.0f);
	set_vec(&dst, 1.0f, 1.0f, 1.0f, 1.0f);
	calc_torque(&dst, &v1, &v2, 0.5f, 0.1f, 1.0f);
	assert_vec(&dst, 4.0f, 2.0f, 8.0f, 5.0f);
}

/* Move from (10, 0, 0) toward the origin by half the distance. w is cleared. */
static void test_calc_torque_moves_and_snaps(void **state)
{
	struct vec3 v1, v2, dst;

	UNUSED_PARAMETER(state);
	set_vec(&v1, 10.0f, 0.0f, 0.0f, 9.0f);
	set_vec(&v2, 0.0f, 0.0f, 0.0f, 4.0f);

	calc_torque(&dst, &v1, &v2, 0.5f, 0.1f, 1.0f);
	assert_vec(&dst, 5.0f, 0.0f, 0.0f, 0.0f);

	/* min_adjust raises a 0.001 step to 0.5. */
	set_vec(&v1, 1.0f, 0.0f, 0.0f, 0.0f);
	calc_torque(&dst, &v1, &v2, 0.001f, 0.5f, 1.0f);
	assert_vec(&dst, 0.5f, 0.0f, 0.0f, 0.0f);

	/* A step that reaches the LARGE_EPSILON gap copies v2, including w. */
	set_vec(&v1, 10.0f, 0.0f, 0.0f, 9.0f);
	calc_torque(&dst, &v1, &v2, 0.5f, 0.1f, 10.0f);
	assert_vec(&dst, 0.0f, 0.0f, 0.0f, 4.0f);
}

/* dst may be v1. The move path used to vec3_add a v1 that vec3_mulf had
 * already overwritten, so an aliased call stepped from -10 instead of 10;
 * the read now happens first, like every other input, and the result
 * matches the non-aliased call. */
static void test_calc_torque_dst_aliases_v1(void **state)
{
	struct vec3 v1, v2, expect;

	UNUSED_PARAMETER(state);
	set_vec(&v1, 10.0f, 0.0f, 0.0f, 9.0f);
	set_vec(&v2, 0.0f, 0.0f, 0.0f, 4.0f);
	calc_torque(&expect, &v1, &v2, 0.5f, 0.1f, 1.0f);
	calc_torque(&v1, &v1, &v2, 0.5f, 0.1f, 1.0f);
	assert_vec(&v1, 5.0f, 0.0f, 0.0f, 0.0f);
	assert_vec(&v1, expect.x, expect.y, expect.z, expect.w);
}

/* Same seed, same first draw. Any non-zero positive_only flag takes the
 * [0, 1] branch. The value itself depends on the C library rand. */
static void test_rand_float_range_and_reseed(void **state)
{
	float a, b, c, d;

	UNUSED_PARAMETER(state);
	srand(1);
	a = rand_float(1);
	srand(1);
	b = rand_float(1);
	assert_true(a == b);
	assert_true(a >= 0.0f && a <= 1.0f);

	srand(1);
	c = rand_float(0);
	assert_true(c >= -1.0f && c <= 1.0f);

	srand(1);
	d = rand_float(2);
	assert_true(d == a);
}

int main(void)
{
	const struct CMUnitTest tests[] = {
		cmocka_unit_test(test_polar_to_cart_plus_z),
		cmocka_unit_test(test_polar_to_cart_zero_radius),
		cmocka_unit_test(test_polar_to_cart_alias),
		cmocka_unit_test(test_cart_to_polar_within_epsilon),
		cmocka_unit_test(test_cart_to_polar_plus_z),
		cmocka_unit_test(test_cart_to_polar_w_lane),
		cmocka_unit_test(test_norm_polar_roundtrip_plus_z),
		cmocka_unit_test(test_calc_torquef_close_returns_target),
		cmocka_unit_test(test_calc_torquef_steps_and_clamps),
		cmocka_unit_test(test_calc_torque_one_sided_close_copies_target),
		cmocka_unit_test(test_calc_torque_moves_and_snaps),
		cmocka_unit_test(test_calc_torque_dst_aliases_v1),
		cmocka_unit_test(test_rand_float_range_and_reseed),
	};

	return cmocka_run_group_tests(tests, NULL, NULL);
}
