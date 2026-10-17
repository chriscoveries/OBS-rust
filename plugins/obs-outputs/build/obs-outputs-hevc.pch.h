/* Precompiled header (BUILD.md) for the plugins/obs-outputs unity groups
 * obs-outputs.hevc. C. Every implementation file in them includes this header and
 * nothing else: the includes and macro setup below are theirs, in group and
 * file order. */

#pragma once

/* rtmp-hevc.c */
#include "../rtmp-hevc.h"
#include "../utils.h"

#include <obs.h>
#include <obs-nal.h>
#include <obs-hevc.h>
#include <util/array-serializer.h>
