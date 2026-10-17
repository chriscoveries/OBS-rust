/* Precompiled header (BUILD.md) for the libobs-winrt unity groups
 * winrt.core. C++. Every implementation file in them includes this header and
 * nothing else: the includes and macro setup below are theirs, in group and
 * file order. */

#pragma once

/* The headers the target precompiled before BUILD.md (WinRT is heavy). */
#include <d3d11.h>
#include <DispatcherQueue.h>
#include <dwmapi.h>
#include <obs-module.h>
#include <util/windows/ComPtr.hpp>
#include <Windows.Graphics.Capture.Interop.h>
#include <windows.graphics.directx.direct3d11.interop.h>
#include <winrt/Windows.Foundation.Metadata.h>
#include <winrt/Windows.Graphics.Capture.h>
#include <winrt/Windows.System.h>

/* winrt-capture.cpp */
#include "winrt-capture.h"
