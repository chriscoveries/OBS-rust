/* Precompiled header (BUILD.md) for the plugins/obs-ffmpeg unity groups
 * obs-ffmpeg.mpegts. C. Every implementation file in them includes this header and
 * nothing else: the includes and macro setup below are theirs, in group and
 * file order. */

#pragma once

/* obs-ffmpeg-mpegts.c */
#include <obs-module.h>
#include <util/deque.h>
#include <util/threading.h>
#include <util/dstr.h>
#include <util/darray.h>
#include <util/platform.h>

#include "../obs-ffmpeg-output.h"
#include "../obs-ffmpeg-formats.h"
#include "../obs-ffmpeg-compat.h"
#include "../obs-ffmpeg-rist.h"
#include "../obs-ffmpeg-srt.h"
#include <libavutil/channel_layout.h>
#include <libavutil/mastering_display_metadata.h>
