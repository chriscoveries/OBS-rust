/* Precompiled header (BUILD.md) for the libobs unity groups libobs.ported-windows, libobs.ported-windows-pipe. C.
 * Every implementation file in them includes this header and nothing else: the includes and macro setup below are
 * theirs, in group and file order. */

#pragma once

/* util/threading-windows.c */
#include "util/bmem.h"
#include "util/threading.h"
#include "util/platform.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#ifdef __MINGW32__
#include <excpt.h>
#ifndef TRYLEVEL_NONE
#ifndef __MINGW64__
#define NO_SEH_MINGW
#endif
#ifndef __try
#define __try
#endif
#ifndef __except
#define __except (x) if (0)
#endif
#endif
#endif

/* util/pipe-windows.c */
#define WIN32_LEAN_AND_MEAN

#include "util/dstr.h"
#include "util/pipe.h"
