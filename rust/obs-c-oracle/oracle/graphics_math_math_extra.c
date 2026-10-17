/* Test-only oracle: the original libobs/graphics/math-extra.c, unmodified, with
 * its global symbols renamed to oracle_* (graphics_math_names.h).
 * The Rust shim provides the unrenamed rand_float that vec3_rand calls. */
#include "graphics_math_names.h"

#include "graphics/math-extra.c.h"
