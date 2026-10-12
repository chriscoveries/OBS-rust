#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <string.h>
#include <cmocka.h>

#include <util/c99defs.h>
#include <graphics/bounds.h>
#include <graphics/plane.h>
#include <graphics/vec2.h>
#include <graphics/vec3.h>

/* Alias safety of the libobs/graphics math functions that only need a snapshot
 * of their input. Each test runs the function twice, once on separate storage
 * and once on the arrangement the 2026-10-12 alias audit measured as broken,
 * and asserts the results are identical. */

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

static void test_bounds_move_vec_aliases_min(void **state)
{
	UNUSED_PARAMETER(state);

	struct bounds src, out, alias;

	set_v3(&src.min, 1.0f, 2.0f, 3.0f);
	set_v3(&src.max, 10.0f, 20.0f, 30.0f);

	bounds_move(&out, &src, &src.min);

	alias = src;
	bounds_move(&alias, &alias, &alias.min);
	assert_true(memcmp(&alias, &out, sizeof(alias)) == 0);
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
	assert_true(memcmp(&alias, &out, sizeof(alias)) == 0);
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

	assert_true(memcmp(&alias, &out, sizeof(alias)) == 0);
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

/* A plane is a 16-byte dir plus a dist; the struct's remaining 12 bytes are
 * padding within its 32-byte size, so the comparison is on the fields. */
static void assert_plane_same(const struct plane *a, const struct plane *b)
{
	assert_memory_equal(&a->dir, &b->dir, sizeof(a->dir));
	assert_true(memcmp(&a->dist, &b->dist, sizeof(a->dist)) == 0);
}

static void test_plane_from_tri_v1_same_address(void **state)
{
	UNUSED_PARAMETER(state);

	struct vec3 v2, v3;
	struct plane p_alias, p_ref;

	set_v3(&p_alias.dir, 0.0f, 0.0f, 1.0f);
	p_alias.dist = 123.0f;
	p_ref = p_alias;
	set_v3(&v2, 1.0f, 0.0f, 0.0f);
	set_v3(&v3, 0.0f, 1.0f, 0.0f);

	/* The reference call reads p_alias.dir, which the aliased call below
	 * writes; the aliased call passes the destination's own base as v1. */
	plane_from_tri(&p_ref, &p_alias.dir, &v2, &v3);
	plane_from_tri(&p_alias, &p_alias.dir, &v2, &v3);

	assert_plane_same(&p_alias, &p_ref);
}

int main(void)
{
	const struct CMUnitTest tests[] = {
		cmocka_unit_test(test_bounds_move_vec_aliases_min),
		cmocka_unit_test(test_bounds_scale_vec_aliases_min),
		cmocka_unit_test(test_bounds_merge_point_vec_aliases_min),
		cmocka_unit_test(test_bounds_get_center_dst_aliases_min),
		cmocka_unit_test(test_plane_from_tri_v1_same_address),
	};

	return cmocka_run_group_tests(tests, NULL, NULL);
}
