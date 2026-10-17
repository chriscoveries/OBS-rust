/* Precompiled header (BUILD.md) for the shared/media-playback unity groups
 * media-playback.core. C. Every implementation file in them includes this header and
 * nothing else: the includes and macro setup below are theirs, in group and
 * file order. */

#pragma once

/* media-playback/decode.c */
#include "media-playback/decode.h"

#include "media-playback/media-playback.h"
#include "media-playback/media.h"
#include <libavutil/mastering_display_metadata.h>

/* media-playback/media-playback.c */
#include "media-playback/cache.h"

/* media-playback/media.c */
#include <util/platform.h>

#include <assert.h>

#include "media-playback/network-timeout.h"
#include "media-playback/closest-format.h"

#include <libavdevice/avdevice.h>
#include <libavutil/imgutils.h>

/* media-playback/network-timeout.c */

#include <util/dstr.h>
