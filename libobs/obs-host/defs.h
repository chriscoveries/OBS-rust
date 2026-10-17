/******************************************************************************
    Copyright (C) 2026 by the obs-rust contributors

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.
******************************************************************************/

#pragma once

/*
 * libobs core host facade (#1051): one header per host capability in
 * libobs/obs-host/. A host that has the capability defines its functions in
 * its own file (obs-windows.c, obs-nix.c, obs-cocoa.m); a host that lacks it
 * builds obs-host/noop/<capability>.c instead, chosen in
 * libobs/cmake/os-<os>.cmake. The core files that call them hold no host
 * conditionals or OS headers, and ci/platform_ifdef_ratchet.py keeps it so.
 *
 * Internal to libobs: none of these headers is installed.
 */

#include "../util/c99defs.h"

/* Declares a facade function: plain `extern` with C linkage, also from C++
 * and Objective-C++ callers, and deliberately without EXPORT. On Windows a
 * __declspec(dllexport) would put these in libobs.dll's export table; they
 * are libobs-internal, like the rest of obs-internal.h. */
#ifdef __cplusplus
#define OBS_HOST_EXTERN extern "C"
#else
#define OBS_HOST_EXTERN extern
#endif
