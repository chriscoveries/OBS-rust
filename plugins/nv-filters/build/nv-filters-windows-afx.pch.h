/* Precompiled header (BUILD.md) for the plugins/nv-filters unity groups
 * nv-filters.windows-afx. C. Every implementation file in them includes this header and
 * nothing else: the includes and macro setup below are theirs, in group and
 * file order. */

#pragma once

/* nvidia-audiofx-filter.c */
#include <stdint.h>
#include <inttypes.h>
#include <obs-module.h>
#include <util/deque.h>
#include <util/threading.h>
#include <media-io/audio-resampler.h>
#include "nvafx-load.h"
#include <pthread.h>
