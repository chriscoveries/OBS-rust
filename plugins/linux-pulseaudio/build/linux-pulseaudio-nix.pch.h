/* Precompiled header (BUILD.md) for the plugins/linux-pulseaudio unity groups
 * linux-pulseaudio.nix. C. Every implementation file in them includes this header and
 * nothing else: the includes and macro setup below are theirs, in group and
 * file order. */

#pragma once

/* linux-pulseaudio.c */
#include <obs-module.h>

/* pulse-input.c */
#include <util/platform.h>
#include <util/bmem.h>

#include "pulse-reconnect.h"
#include "pulse-timing.h"
#include "pulse-wrapper.h"

/* pulse-reconnect.c */
#include <string.h>

#include <pulse/timeval.h>

/* pulse-timing.c */
#include <stdint.h>

#include <util/util_uint64.h>

/* pulse-wrapper.c */
#include <pthread.h>

#include <pulse/thread-mainloop.h>
#include <pulse/rtclock.h>

#include <util/base.h>
#include <util/darray.h>
#include <obs.h>
