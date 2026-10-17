/* Precompiled header: Qt UI targets (cmake/common/pch.cmake): the whole of
 * QtCore, QtGui and QtWidgets, compiled once per target instead of once per
 * source.
 *
 * Qt comes before the libobs headers: libobs's graphics headers map x86
 * intrinsics through SIMDe's native aliases, which break Qt's own
 * <smmintrin.h> include if they are seen first. */

#pragma once

#include "obs-cxx-qt-std.hpp"

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
#include <util/dstr.h>
#include <util/platform.h>
#include <util/threading.h>
#include <obs-module.h>
#include <obs.hpp>
#include <util/util.hpp>

#ifdef OBS_PCH_RESTORE_LEAN
#undef WIN32_LEAN_AND_MEAN
#undef OBS_PCH_RESTORE_LEAN
#endif
