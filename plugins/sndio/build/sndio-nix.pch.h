/* Precompiled header (BUILD.md) for the plugins/sndio unity groups
 * sndio.nix. C. Every implementation file in them includes this header and
 * nothing else: the includes and macro setup below are theirs, in group and
 * file order. */

#pragma once

/* sndio-input.c */
#include <sys/types.h>
#include <sys/socket.h>
#include <errno.h>
#include <poll.h>
#include <pthread.h>
#include <sndio.h>
#include <string.h>
#include <unistd.h>

#include <util/bmem.h>
#include <util/platform.h>
#include <util/threading.h>
#include <util/util_uint64.h>
#include <obs.h>
#include <obs-module.h>

#include "sndio-input.h"
