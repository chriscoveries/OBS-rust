/* Precompiled header (BUILD.md) for the plugins/obs-filters unity groups
 * obs-filters.noise-suppress. C. Every implementation file in them includes this header and
 * nothing else: the includes and macro setup below are theirs, in group and
 * file order. */

#pragma once

/* noise-suppress-filter.c */
#include <stdint.h>
#include <inttypes.h>

#include <util/deque.h>
#include <util/threading.h>
#include <obs-module.h>

#ifdef LIBSPEEXDSP_ENABLED
#include <speex/speex_preprocess.h>
#endif

#ifdef LIBRNNOISE_ENABLED
/* MSVC lacks ssize_t; CMake defines it for this file (cmake/rnnoise.cmake). */
#include <rnnoise.h>
#include <media-io/audio-resampler.h>
#endif
