/* Precompiled header (BUILD.md) for the plugins/linux-alsa unity groups
 * linux-alsa.nix. C. Every implementation file in them includes this header and
 * nothing else: the includes and macro setup below are theirs, in group and
 * file order. */

#pragma once

/* alsa-input.c */
#include <util/bmem.h>
#include <util/platform.h>
#include <util/threading.h>
#include <util/util_uint64.h>
#include <obs-module.h>

#include <alsa/asoundlib.h>
#include <alsa/pcm.h>

#include <pthread.h>
