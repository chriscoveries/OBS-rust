/* Precompiled header (BUILD.md) for the plugins/obs-outputs unity groups
 * obs-outputs.mp4-mux. C. Every implementation file in them includes this header and
 * nothing else: the includes and macro setup below are theirs, in group and
 * file order. */

#pragma once

/* mp4-mux.c */
#include "../mp4-mux-internal.h"

#include "../rtmp-hevc.h"
#include "../rtmp-av1.h"

#include <obs-avc.h>
#include <obs-hevc.h>
#include <obs-module.h>
#include <util/dstr.h>
#include <util/platform.h>
#include <util/array-serializer.h>

#include <time.h>
