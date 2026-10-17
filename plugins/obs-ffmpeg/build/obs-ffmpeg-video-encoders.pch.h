/* Precompiled header (BUILD.md) for the plugins/obs-ffmpeg unity groups
 * obs-ffmpeg.video-encoders, obs-ffmpeg.nvenc. C. Every implementation file in them includes this header and
 * nothing else: the includes and macro setup below are theirs, in group and
 * file order. */

#pragma once

/* obs-ffmpeg-av1.c */
#include "../obs-ffmpeg-video-encoders.h"

/* obs-ffmpeg-video-encoders.c */
#include <util/threading.h>

/* obs-ffmpeg-nvenc.c */
#include <obs-avc.h>
#ifdef ENABLE_HEVC
#include <obs-hevc.h>
#endif
