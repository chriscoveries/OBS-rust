#include <math.h>
#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <cmocka.h>

#include <graphics/matrix3.h>
#include <graphics/matrix4.h>
#include <graphics/plane.h>
#include <util/c99defs.h>

/* Characterization of the exported functions in graphics/plane.c.
 * Odd lane and failure behavior is characterized, not endorsed. */

static void set_vec(struct vec3 *v, float x, float y, float z, float w)
{
	vec3_set(v, x, y, z);
	v->w = w;
}

static void assert_vec(const struct vec3 *v, float x, float y, float z, float w)
{
	assert_true(v->x == x && v->y == y && v->z == z && v->w == w);
}

static void assert_plane(const struct plane *p, float x, float y, float z, float w, float dist)
{
	assert_vec(&p->dir, x, y, z, w);
	assert_true(p->dist == dist);
}

/* Unit XY triangle through the origin. The normal's w lane is 0. */
static void test_plane_from_tri_xy(void **state)
{
	struct vec3 v1, v2, v3;
	struct plane p;

	UNUSED_PARAMETER(state);
	set_vec(&v1, 0.0f, 0.0f, 0.0f, 0.0f);
	set_vec(&v2, 1.0f, 0.0f, 0.0f, 0.0f);
	set_vec(&v3, 0.0f, 1.0f, 0.0f, 0.0f);
	plane_from_tri(&p, &v1, &v2, &v3);
	assert_plane(&p, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f);
}

/* The same triangle lifted to z = 1. dist is the dot with v1. */
static void test_plane_from_tri_offset(void **state)
{
	struct vec3 v1, v2, v3;
	struct plane p;

	UNUSED_PARAMETER(state);
	set_vec(&v1, 0.0f, 0.0f, 1.0f, 0.0f);
	set_vec(&v2, 1.0f, 0.0f, 1.0f, 0.0f);
	set_vec(&v3, 0.0f, 1.0f, 1.0f, 0.0f);
	plane_from_tri(&p, &v1, &v2, &v3);
	assert_plane(&p, 0.0f, 0.0f, 1.0f, 0.0f, 1.0f);
}

/* A collapsed triangle normalizes to the zero vector, not "unchanged". */
static void test_plane_from_tri_degenerate(void **state)
{
	struct vec3 v1, v2, v3;
	struct plane p;

	UNUSED_PARAMETER(state);
	set_vec(&v1, 0.0f, 0.0f, 0.0f, 0.0f);
	set_vec(&v2, 1.0f, 0.0f, 0.0f, 0.0f);
	set_vec(&v3, 2.0f, 0.0f, 0.0f, 0.0f);
	p.dist = 9.0f;
	plane_from_tri(&p, &v1, &v2, &v3);
	assert_plane(&p, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f);
}

/* v1 is dst->dir. The distance used to dot the normalized dir, because the
 * vertex was read after dst->dir was written and normalized; the vertex is
 * now read first, so the aliased call matches the non-aliased one. */
static void test_plane_from_tri_v1_aliases_dir(void **state)
{
	struct vec3 v1, v2, v3;
	struct plane p, expect;

	UNUSED_PARAMETER(state);
	set_vec(&v1, 0.0f, 0.0f, 1.0f, 0.0f);
	set_vec(&v2, 1.0f, 0.0f, 0.0f, 0.0f);
	set_vec(&v3, 0.0f, 1.0f, 0.0f, 0.0f);

	set_vec(&p.dir, 0.0f, 0.0f, 1.0f, 0.0f);
	p.dist = 9.0f;
	plane_from_tri(&p, &p.dir, &v2, &v3);
	assert_plane(&p, 0.57735026f, 0.57735026f, 0.57735026f, 0.0f, 0.57735026f);

	plane_from_tri(&expect, &v1, &v2, &v3);
	assert_plane(&p, expect.dir.x, expect.dir.y, expect.dir.z, expect.dir.w, expect.dist);
}

/* vec3_sub clears w before the cross, but the final dot still multiplies
 * v1.w. A NaN lane makes dist NaN while the normal stays +Z.
 * Characterized, not endorsed. */
static void test_plane_from_tri_nan_w(void **state)
{
	struct vec3 v1, v2, v3;
	struct plane p;

	UNUSED_PARAMETER(state);
	set_vec(&v1, 0.0f, 0.0f, 0.0f, NAN);
	set_vec(&v2, 1.0f, 0.0f, 0.0f, 0.0f);
	set_vec(&v3, 0.0f, 1.0f, 0.0f, 0.0f);
	plane_from_tri(&p, &v1, &v2, &v3);
	assert_vec(&p.dir, 0.0f, 0.0f, 1.0f, 0.0f);
	assert_true(isnan(p.dist));
}

/* vec3_transform treats the normal as a point. A pure Z translation of a
 * +Z plane keeps the normal and subtracts the translation from dist. */
static void test_plane_transform_translation(void **state)
{
	struct plane p, dst;
	struct matrix4 m;

	UNUSED_PARAMETER(state);
	plane_setf(&p, 0.0f, 0.0f, 1.0f, 1.0f);
	matrix4_identity(&m);
	vec4_set(&m.t, 0.0f, 0.0f, 3.0f, 1.0f);
	plane_transform(&dst, &p, &m);
	assert_plane(&dst, 0.0f, 0.0f, 1.0f, 0.0f, -2.0f);
}

/* 90 degrees about X sends +Z to -Y. dist is unchanged when t is 0. */
static void test_plane_transform_rotates_normal(void **state)
{
	struct plane p, dst;
	struct matrix4 m;

	UNUSED_PARAMETER(state);
	plane_setf(&p, 0.0f, 0.0f, 1.0f, 4.0f);
	matrix4_identity(&m);
	vec4_set(&m.y, 0.0f, 0.0f, 1.0f, 0.0f);
	vec4_set(&m.z, 0.0f, -1.0f, 0.0f, 0.0f);
	plane_transform(&dst, &p, &m);
	assert_plane(&dst, 0.0f, -1.0f, 0.0f, 0.0f, 4.0f);
}

/* transform3x4 subtracts t before rotating. A +Z normal minus t=(0,0,5)
 * flips to -Z. The transformed translation is 0, so dist stays. */
static void test_plane_transform3x4_flips_normal(void **state)
{
	struct plane p, dst;
	struct matrix3 m;

	UNUSED_PARAMETER(state);
	plane_setf(&p, 0.0f, 0.0f, 1.0f, 1.0f);
	matrix3_identity(&m);
	vec3_set(&m.t, 0.0f, 0.0f, 5.0f);
	plane_transform3x4(&dst, &p, &m);
	assert_plane(&dst, 0.0f, 0.0f, -1.0f, 0.0f, 1.0f);
}

/* Subtracting t from the normal lands on the origin, so the result normal
 * is zero and dist is unchanged. Characterized, not endorsed. */
static void test_plane_transform3x4_zero_normal(void **state)
{
	struct plane p, dst;
	struct matrix3 m;

	UNUSED_PARAMETER(state);
	plane_setf(&p, 1.0f, 0.0f, 0.0f, 4.0f);
	matrix3_identity(&m);
	vec3_set(&m.t, 1.0f, 0.0f, 0.0f);
	plane_transform3x4(&dst, &p, &m);
	assert_plane(&dst, 0.0f, 0.0f, 0.0f, 0.0f, 4.0f);
}

static void test_plane_intersection_ray_hit(void **state)
{
	struct plane p;
	struct vec3 orig, dir;
	float t = 42.0f;

	UNUSED_PARAMETER(state);
	plane_setf(&p, 0.0f, 0.0f, 1.0f, 4.0f);
	set_vec(&orig, 0.0f, 0.0f, 0.0f, 0.0f);
	set_vec(&dir, 0.0f, 0.0f, 1.0f, 0.0f);
	assert_true(plane_intersection_ray(&p, &orig, &dir, &t));
	assert_true(t == 4.0f);
}

/* A ray pointing away still returns true, with a negative t. */
static void test_plane_intersection_ray_behind(void **state)
{
	struct plane p;
	struct vec3 orig, dir;
	float t = 42.0f;

	UNUSED_PARAMETER(state);
	plane_setf(&p, 0.0f, 0.0f, 1.0f, 4.0f);
	set_vec(&orig, 0.0f, 0.0f, 10.0f, 0.0f);
	set_vec(&dir, 0.0f, 0.0f, 1.0f, 0.0f);
	assert_true(plane_intersection_ray(&p, &orig, &dir, &t));
	assert_true(t == -6.0f);
}

/* fabs(c) < EPSILON (1e-4) is a miss and writes 0. Equal to EPSILON hits.
 * Characterized, not endorsed. */
static void test_plane_intersection_ray_epsilon(void **state)
{
	struct plane p;
	struct vec3 orig, dir;
	float t;

	UNUSED_PARAMETER(state);
	plane_setf(&p, 1.0f, 0.0f, 0.0f, 1.0f);
	set_vec(&orig, 0.0f, 0.0f, 0.0f, 0.0f);

	set_vec(&dir, 0.0f, 1.0f, 0.0f, 0.0f);
	t = 42.0f;
	assert_false(plane_intersection_ray(&p, &orig, &dir, &t));
	assert_true(t == 0.0f);

	set_vec(&dir, 0.00005f, 0.0f, 0.0f, 0.0f);
	t = 42.0f;
	assert_false(plane_intersection_ray(&p, &orig, &dir, &t));
	assert_true(t == 0.0f);

	set_vec(&dir, 1e-4f, 0.0f, 0.0f, 0.0f);
	t = 42.0f;
	assert_true(plane_intersection_ray(&p, &orig, &dir, &t));
	assert_true(t == 10000.0f);
}

static void test_plane_intersection_line_crosses(void **state)
{
	struct plane p;
	struct vec3 v1, v2;
	float t = 42.0f;

	UNUSED_PARAMETER(state);
	plane_setf(&p, 0.0f, 0.0f, 1.0f, 1.0f);
	set_vec(&v1, 0.0f, 0.0f, 0.0f, 0.0f);
	set_vec(&v2, 0.0f, 0.0f, 2.0f, 0.0f);
	assert_true(plane_intersection_line(&p, &v1, &v2, &t));
	assert_true(t == 0.5f);

	set_vec(&v1, 0.0f, 0.0f, 1.0f, 0.0f);
	set_vec(&v2, 0.0f, 0.0f, 3.0f, 0.0f);
	assert_true(plane_intersection_line(&p, &v1, &v2, &t));
	assert_true(t == 0.0f);

	set_vec(&v1, 0.0f, 0.0f, 3.0f, 0.0f);
	set_vec(&v2, 0.0f, 0.0f, 1.0f, 0.0f);
	assert_true(plane_intersection_line(&p, &v1, &v2, &t));
	assert_true(t == 1.0f);
}

/* A miss does not write *t. Both endpoints on the plane is also a miss.
 * Characterized, not endorsed. */
static void test_plane_intersection_line_miss_leaves_t(void **state)
{
	struct plane p;
	struct vec3 v1, v2;
	float t;

	UNUSED_PARAMETER(state);
	plane_setf(&p, 0.0f, 0.0f, 1.0f, 1.0f);

	set_vec(&v1, 0.0f, 0.0f, 1.0f, 0.0f);
	set_vec(&v2, 1.0f, 0.0f, 1.0f, 0.0f);
	t = 42.0f;
	assert_false(plane_intersection_line(&p, &v1, &v2, &t));
	assert_true(t == 42.0f);

	set_vec(&v1, 0.0f, 0.0f, 2.0f, 0.0f);
	set_vec(&v2, 0.0f, 0.0f, 4.0f, 0.0f);
	t = 42.0f;
	assert_false(plane_intersection_line(&p, &v1, &v2, &t));
	assert_true(t == 42.0f);
}

/* vec3_plane_dist dots the w lanes. A nonzero w puts v1 on the plane even
 * though its xyz is at the origin. Characterized, not endorsed. */
static void test_plane_intersection_line_w_lane(void **state)
{
	struct plane p;
	struct vec3 v1, v2;
	float t = 42.0f;

	UNUSED_PARAMETER(state);
	plane_setf(&p, 0.0f, 0.0f, 1.0f, 6.0f);
	p.dir.w = 2.0f;
	set_vec(&v1, 0.0f, 0.0f, 0.0f, 3.0f);
	set_vec(&v2, 0.0f, 0.0f, 2.0f, 0.0f);
	assert_true(plane_intersection_line(&p, &v1, &v2, &t));
	assert_true(t == 0.0f);
}

/* "Inside" is true when any point is outside the open slab (-precision,
 * precision). A triangle lying in the slab returns false. */
static void test_plane_tri_inside_slab(void **state)
{
	struct plane p;
	struct vec3 a, b, c, out;

	UNUSED_PARAMETER(state);
	plane_setf(&p, 0.0f, 0.0f, 1.0f, 0.0f);
	set_vec(&a, 0.0f, 0.0f, 0.0f, 0.0f);
	set_vec(&b, 1.0f, 0.0f, 0.05f, 0.0f);
	set_vec(&c, 0.0f, 1.0f, -0.05f, 0.0f);
	assert_false(plane_tri_inside(&p, &a, &b, &c, 0.1f));

	set_vec(&out, 0.0f, 0.0f, 0.1f, 0.0f);
	assert_true(plane_tri_inside(&p, &a, &b, &out, 0.1f));

	set_vec(&out, 0.0f, 0.0f, -0.1f, 0.0f);
	assert_true(plane_tri_inside(&p, &a, &b, &out, 0.1f));
}

static void test_plane_line_inside_slab(void **state)
{
	struct plane p;
	struct vec3 a, b;

	UNUSED_PARAMETER(state);
	plane_setf(&p, 0.0f, 0.0f, 1.0f, 0.0f);
	set_vec(&a, 0.0f, 0.0f, 0.05f, 0.0f);
	set_vec(&b, 1.0f, 0.0f, -0.05f, 0.0f);
	assert_false(plane_line_inside(&p, &a, &b, 0.1f));

	set_vec(&b, 1.0f, 0.0f, 0.1f, 0.0f);
	assert_true(plane_line_inside(&p, &a, &b, 0.1f));
}

int main(void)
{
	const struct CMUnitTest tests[] = {
		cmocka_unit_test(test_plane_from_tri_xy),
		cmocka_unit_test(test_plane_from_tri_offset),
		cmocka_unit_test(test_plane_from_tri_degenerate),
		cmocka_unit_test(test_plane_from_tri_v1_aliases_dir),
		cmocka_unit_test(test_plane_from_tri_nan_w),
		cmocka_unit_test(test_plane_transform_translation),
		cmocka_unit_test(test_plane_transform_rotates_normal),
		cmocka_unit_test(test_plane_transform3x4_flips_normal),
		cmocka_unit_test(test_plane_transform3x4_zero_normal),
		cmocka_unit_test(test_plane_intersection_ray_hit),
		cmocka_unit_test(test_plane_intersection_ray_behind),
		cmocka_unit_test(test_plane_intersection_ray_epsilon),
		cmocka_unit_test(test_plane_intersection_line_crosses),
		cmocka_unit_test(test_plane_intersection_line_miss_leaves_t),
		cmocka_unit_test(test_plane_intersection_line_w_lane),
		cmocka_unit_test(test_plane_tri_inside_slab),
		cmocka_unit_test(test_plane_line_inside_slab),
	};

	return cmocka_run_group_tests(tests, NULL, NULL);
}
