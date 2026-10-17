/* Precompiled header (BUILD.md) for the plugins/obs-filters unity groups
 * obs-filters.core. C. Every implementation file in them includes this header and
 * nothing else: the includes and macro setup below are theirs, in group and
 * file order. */

#pragma once

/* async-delay-filter.c */
#include <obs-module.h>
#include <util/deque.h>
#include <util/util_uint64.h>

/* chroma-key-filter.c */
#include <graphics/matrix4.h>
#include <graphics/vec2.h>
#include <graphics/vec4.h>

/* color-correction-filter.c */
#include <graphics/quat.h>

/* color-grade-filter.c */
#include <graphics/half.h>
#include <graphics/image-file.h>
#include <util/dstr.h>
#include <util/platform.h>

#include "../cube-lut.h"

/* compressor-filter.c */
#include <stdint.h>
#include <inttypes.h>
#include <math.h>

#include <media-io/audio-math.h>
#include <util/threading.h>

/* cube-lut.c */
#include <stdio.h>
#include <string.h>

#include <graphics/srgb.h>
#include <util/base.h>
#include <util/bmem.h>

/* eq-filter.c */
#include <util/darray.h>

/* mask-filter.c */
#include <sys/stat.h>

/* scale-filter.c */
#include <stdlib.h>
#include <graphics/math-defs.h>

/* sharpness-filter.c */
#include <obs-source.h>
#include <obs.h>
