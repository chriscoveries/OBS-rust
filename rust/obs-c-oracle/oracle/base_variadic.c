/* base.c's variadic entry points stay C in libobs (util/base-variadic.c.h);
 * the oracle needs them too. A wrapper rather than a direct build.rs file:
 * implementation files end in .h (BUILD.md), which a compiler treats as a
 * header to precompile. */

#include "util/base-variadic.c.h"
