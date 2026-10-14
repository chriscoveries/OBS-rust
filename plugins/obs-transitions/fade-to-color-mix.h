#pragma once

/* Pure math of the Fade to Color transition, kept free of libobs so a test
 * can call it without a display. */

static inline float ftc_lerp(float a, float b, float x)
{
	return (1.0f - x) * a + x * b;
}

static inline float ftc_clamp(float x, float min, float max)
{
	if (x < min)
		return min;
	else if (x > max)
		return max;
	return x;
}

static inline float ftc_smoothstep(float min, float max, float x)
{
	/* A zero-width ramp is a step; dividing by it gave NaN or infinity. */
	if (!(max > min))
		return x >= max ? 1.0f : 0.0f;
	x = ftc_clamp((x - min) / (max - min), 0.0f, 1.0f);
	return x * x * (3 - 2 * x);
}

/* Gain of the outgoing source at progress t for a switch point sp in [0,1]. */
static inline float ftc_mix_a(float sp, float t)
{
	return ftc_lerp(1.0f - t, 0.0f, ftc_smoothstep(0.0f, sp, t));
}

/* Gain of the incoming source at progress t for a switch point sp in [0,1]. */
static inline float ftc_mix_b(float sp, float t)
{
	return ftc_lerp(0.0f, t, ftc_smoothstep(sp, 1.0f, t));
}
