/******************************************************************************
    Copyright (C) 2023 by Lain Bailey <lain@obsproject.com>

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.
******************************************************************************/

#include "quat.h"
#include "vec3.h"
#include "matrix3.h"
#include "matrix4.h"
#include "axisang.h"

static inline void quat_vec3(struct vec3 *v, const struct quat *q)
{
	v->m = q->m;
	v->w = 0.0f;
}

/* The scalar part of the product, in its own no-inlined body: a compiler is
 * otherwise free to merge the multiply into the vector work above it, which
 * rounds the result differently (the separate form is one fused
 * multiply-subtract of a pairwise-reduced dot product). */
#if defined(_MSC_VER)
#define QUAT_NOINLINE(fn) static float fn
#else
#define QUAT_NOINLINE(fn) static float __attribute__((noinline)) fn
#endif

QUAT_NOINLINE(quat_mul_w)(const struct vec3 *q1axis, const struct vec3 *q2axis, float w1, float w2)
{
	return (w1 * w2) - vec3_dot(q1axis, q2axis);
}

void quat_mul(struct quat *dst, const struct quat *q1, const struct quat *q2)
{
	struct vec3 q1axis, q2axis;
	struct vec3 temp1, temp2;
	/* The scalars are read before the vector part is stored, because a
	 * caller may pass dst overlapping q1 or q2 and the store below
	 * overwrites dst->w. */
	volatile float w1, w2;

	quat_vec3(&q1axis, q1);
	quat_vec3(&q2axis, q2);

	vec3_mulf(&temp1, &q2axis, q1->w);
	vec3_mulf(&temp2, &q1axis, q2->w);

	w1 = q1->w;
	w2 = q2->w;

	vec3_add(&temp1, &temp1, &temp2);
	vec3_cross(&temp2, &q1axis, &q2axis);
	vec3_add((struct vec3 *)dst, &temp1, &temp2);

	dst->w = quat_mul_w(&q1axis, &q2axis, w1, w2);
}

void quat_from_axisang(struct quat *dst, const struct axisang *aa)
{
	float halfa = aa->w * 0.5f;
	float sine = sinf(halfa);

	dst->x = aa->x * sine;
	dst->y = aa->y * sine;
	dst->z = aa->z * sine;
	dst->w = cosf(halfa);
}

void quat_from_matrix3(struct quat *dst, const struct matrix3 *m)
{
	quat_from_matrix4(dst, (const struct matrix4 *)m);
}

void quat_from_matrix4(struct quat *dst, const struct matrix4 *m)
{
	/* The 3x3 block is snapshotted first: quat_from_matrix3 hands a matrix3
	 * cast to a matrix4, and a caller may place dst at the same address as
	 * m, so no read below may observe a quaternion lane written earlier. */
	float mm[3][3];
	float tr, inv_half, four_d;
	int i, j, k;

	mm[0][0] = m->x.x;
	mm[0][1] = m->x.y;
	mm[0][2] = m->x.z;
	mm[1][0] = m->y.x;
	mm[1][1] = m->y.y;
	mm[1][2] = m->y.z;
	mm[2][0] = m->z.x;
	mm[2][1] = m->z.y;
	mm[2][2] = m->z.z;

	tr = (mm[0][0] + mm[1][1] + mm[2][2]);

	if (tr > 0.0f) {
		four_d = sqrtf(tr + 1.0f);
		dst->w = four_d * 0.5f;

		inv_half = 0.5f / four_d;
		dst->x = (mm[1][2] - mm[2][1]) * inv_half;
		dst->y = (mm[2][0] - mm[0][2]) * inv_half;
		dst->z = (mm[0][1] - mm[1][0]) * inv_half;
	} else {
		i = (mm[0][0] > mm[1][1]) ? 0 : 1;

		if (mm[2][2] > mm[i][i])
			i = 2;

		j = (i + 1) % 3;
		k = (i + 2) % 3;

		/* ---------------------------------- */

		four_d = sqrtf((mm[i][i] - mm[j][j] - mm[k][k]) + 1.0f);

		dst->ptr[i] = four_d * 0.5f;

		inv_half = 0.5f / four_d;
		dst->ptr[j] = (mm[i][j] + mm[j][i]) * inv_half;
		dst->ptr[k] = (mm[i][k] + mm[k][i]) * inv_half;
		dst->w = (mm[j][k] - mm[k][j]) * inv_half;
	}
}

void quat_get_dir(struct vec3 *dst, const struct quat *q)
{
	struct matrix3 m;
	matrix3_from_quat(&m, q);
	vec3_copy(dst, &m.z);
}

void quat_set_look_dir(struct quat *dst, const struct vec3 *dir)
{
	struct vec3 new_dir;
	struct quat xz_rot, yz_rot;
	bool xz_valid;
	bool yz_valid;
	struct axisang aa;

	vec3_norm(&new_dir, dir);
	vec3_neg(&new_dir, &new_dir);

	quat_identity(&xz_rot);
	quat_identity(&yz_rot);

	xz_valid = close_float(new_dir.x, 0.0f, EPSILON) || close_float(new_dir.z, 0.0f, EPSILON);
	yz_valid = close_float(new_dir.y, 0.0f, EPSILON);

	if (xz_valid) {
		axisang_set(&aa, 0.0f, 1.0f, 0.0f, atan2f(new_dir.x, new_dir.z));

		quat_from_axisang(&xz_rot, &aa);
	}
	if (yz_valid) {
		axisang_set(&aa, -1.0f, 0.0f, 0.0f, asinf(new_dir.y));
		quat_from_axisang(&yz_rot, &aa);
	}

	if (!xz_valid)
		quat_copy(dst, &yz_rot);
	else if (!yz_valid)
		quat_copy(dst, &xz_rot);
	else
		quat_mul(dst, &xz_rot, &yz_rot);
}

void quat_log(struct quat *dst, const struct quat *q)
{
	float angle = acosf(q->w);
	float sine = sinf(angle);
	float w = q->w;

	quat_copy(dst, q);
	dst->w = 0.0f;

	if ((fabsf(w) < 1.0f) && (fabsf(sine) >= EPSILON)) {
		sine = angle / sine;
		quat_mulf(dst, dst, sine);
	}
}

void quat_exp(struct quat *dst, const struct quat *q)
{
	float length = sqrtf(q->x * q->x + q->y * q->y + q->z * q->z);
	float sine = sinf(length);

	quat_copy(dst, q);
	sine = (length > EPSILON) ? (sine / length) : 1.0f;
	quat_mulf(dst, dst, sine);
	dst->w = cosf(length);
}

void quat_interpolate(struct quat *dst, const struct quat *q1, const struct quat *q2, float t)
{
	float dot = quat_dot(q1, q2);
	float anglef = acosf(dot);
	float sine, sinei, sinet, sineti;
	struct quat temp;

	if (anglef >= EPSILON) {
		sine = sinf(anglef);
		sinei = 1 / sine;
		sinet = sinf(anglef * t) * sinei;
		sineti = sinf(anglef * (1.0f - t)) * sinei;

		quat_mulf(&temp, q1, sineti);
		quat_mulf(dst, q2, sinet);
		quat_add(dst, &temp, dst);
	} else {
		quat_sub(&temp, q2, q1);
		quat_mulf(&temp, &temp, t);
		quat_add(dst, &temp, q1);
	}
}

void quat_get_tangent(struct quat *dst, const struct quat *prev, const struct quat *q, const struct quat *next)
{
	struct quat temp;

	quat_sub(&temp, q, prev);
	quat_add(&temp, &temp, next);
	quat_sub(&temp, &temp, q);
	quat_mulf(dst, &temp, 0.5f);
}

void quat_interpolate_cubic(struct quat *dst, const struct quat *q1, const struct quat *q2, const struct quat *m1,
			    const struct quat *m2, float t)
{
	struct quat temp1, temp2;

	quat_interpolate(&temp1, q1, q2, t);
	quat_interpolate(&temp2, m1, m2, t);
	quat_interpolate(dst, &temp1, &temp2, 2.0f * (1.0f - t) * t);
}
