/* Precompiled header (BUILD.md) for the plugins/obs-ffmpeg unity groups
 * obs-ffmpeg.source. C. Every implementation file in them includes this header and
 * nothing else: the includes and macro setup below are theirs, in group and
 * file order. */

#pragma once

/* obs-ffmpeg-source.c */
#include <obs-module.h>
#include <util/threading.h>
#include <util/platform.h>
#include <util/dstr.h>

#include "../obs-ffmpeg-compat.h"
#include "../obs-ffmpeg-formats.h"

#include <media-playback/media-playback.h>
