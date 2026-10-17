/* Precompiled header (BUILD.md) for the libobs unity groups libobs.ported-media, libobs.ported-hevc, libobs.ported-data. C.
 * Every implementation file in them includes this header and nothing else: the includes and macro setup below are
 * theirs, in group and file order. */

#pragma once

/* media-io/audio-io.c */
#include <math.h>
#include <inttypes.h>

#include "util/threading.h"
#include "util/darray.h"
#include "util/deque.h"
#include "util/platform.h"
#include "util/profiler.h"
#include "util/util_uint64.h"

#include "media-io/audio-io.h"
#include "media-io/audio-resampler.h"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <avrt.h>
#endif

/* media-io/format-conversion.c */
#include "media-io/format-conversion.h"

#include "util/sse-intrin.h"

/* media-io/video-fourcc.c */
#include "util/c99defs.h"
#include "media-io/video-io.h"

/* media-io/video-frame.c */
#include <assert.h>
#include "media-io/video-frame.h"

/* media-io/video-io.c */
#include "util/bmem.h"

#include "media-io/video-scaler.h"

/* media-io/video-matrices.c */

#include "graphics/matrix3.h"

/* obs-av1.c */
#include "obs-av1.h"

#include "obs.h"

/* obs-avc.c */
#include "obs-avc.h"

#include "obs-nal.h"
#include "util/array-serializer.h"
#include "util/bitstream.h"

/* obs-hotkey-name-map.c */
#include <string.h>

#include <util/bmem.h>
#include <util/c99defs.h>

#include "obs-internal.h"

/* obs-properties.c */
#include "obs-properties.h"

/* obs-hevc.c */
#include "obs-hevc.h"

/* obs-data.c */
#include "util/dstr.h"
#include "util/uthash.h"
#include "graphics/vec2.h"
#include "graphics/vec3.h"
#include "graphics/vec4.h"
#include "graphics/quat.h"
#include "obs-data.h"

#include <jansson.h>
