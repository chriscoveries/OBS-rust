/* Precompiled header (BUILD.md) for the plugins/oss-audio unity groups
 * oss-audio.core. C. Every implementation file in them includes this header and
 * nothing else: the includes and macro setup below are theirs, in group and
 * file order. */

#pragma once

/* oss-audio.c */
#include <obs-module.h>

/* oss-input.c */
#include <util/bmem.h>
#include <util/platform.h>
#include <util/threading.h>
#include "oss-input-platform.h"

#include <ctype.h>
#include <poll.h>
#include <unistd.h>
#include <fcntl.h>
#include <pthread.h>
#include <sys/soundcard.h>
