/* Precompiled header (BUILD.md) for the plugins/win-wasapi unity groups
 * win-wasapi.windows. C++. Every implementation file in them includes this header and
 * nothing else: the includes and macro setup below are theirs, in group and
 * file order. */

#pragma once

/* enum-wasapi.cpp */
#include "enum-wasapi.hpp"

#include <util/base.h>
#include <util/platform.h>
#include <util/windows/HRError.hpp>
#include <util/windows/ComPtr.hpp>
#include <util/windows/CoTaskMemPtr.hpp>

/* plugin-main.cpp */
#include "wasapi-notify.hpp"

#include <obs-module.h>

#include <util/windows/win-version.h>

/* wasapi-notify.cpp */

#include <windows.h>
#include <assert.h>

#include <util/threading.h>

/* win-wasapi.cpp */

#include <obs.h>
#include <util/dstr.h>
#include <util/windows/WinHandle.hpp>
#include <util/windows/window-helpers.h>
#include <util/util_uint64.h>

#include <atomic>
#include <cinttypes>

#include <audioclientactivationparams.h>
#include <avrt.h>
#include <RTWorkQ.h>
#include <wrl/implements.h>
