/* Precompiled header: C sources that link libobs (plugins, shared libraries,
 * cmake/common/pch.cmake). The libobs headers most of them include. */

#pragma once

#include "obs-c-std.h"

/* Some libobs headers (util/sse-intrin.h on non-x86 Windows) define
 * WIN32_LEAN_AND_MEAN and leave it defined. A source that includes the full
 * <windows.h> (winsock types such as u_long) before any libobs header must not
 * see it, so the macro is restored after the libobs headers below. */
#if defined(_WIN32) && !defined(WIN32_LEAN_AND_MEAN)
#define OBS_PCH_RESTORE_LEAN
#endif

#include <util/c99defs.h>
#include <util/base.h>
#include <util/bmem.h>
#include <util/darray.h>
#include <util/deque.h>
#include <util/dstr.h>
#include <util/platform.h>
#include <util/threading.h>
#include <util/util_uint64.h>
#include <obs-module.h>

#ifdef OBS_PCH_RESTORE_LEAN
#undef WIN32_LEAN_AND_MEAN
#undef OBS_PCH_RESTORE_LEAN
#endif
