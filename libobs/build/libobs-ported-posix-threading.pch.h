/* Precompiled header (BUILD.md) for the libobs unity groups libobs.ported-posix-threading. C.
 * Every implementation file in them includes this header and nothing else: the includes and macro setup below are
 * theirs, in group and file order. */

#pragma once

/* util/threading-posix.c */
#if defined(__APPLE__) || defined(__MINGW32__)
#include <sys/time.h>
#endif
#ifdef __APPLE__
#include <mach/semaphore.h>
#include <mach/task.h>
#include <mach/mach_init.h>
#else
#define _GNU_SOURCE
#include <semaphore.h>
#endif

#if defined(__FreeBSD__)
#include <pthread_np.h>
#endif

#include "util/bmem.h"
#include "util/threading.h"
