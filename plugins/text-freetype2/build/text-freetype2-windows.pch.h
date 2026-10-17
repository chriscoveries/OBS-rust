/* Precompiled header (BUILD.md) for the plugins/text-freetype2 unity groups
 * text-freetype2.windows. C. Every implementation file in them includes this header and
 * nothing else: the includes and macro setup below are theirs, in group and
 * file order. */

#pragma once

/* find-font-windows.c */
#include <util/dstr.h>
#include <util/darray.h>
#include <util/crc32.h>
#include "find-font.h"
#include "text-freetype2.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>
#include <shlobj.h>

/* text-freetype2-host-windows.c */
#include <obs-module.h>
#include "text-freetype2-host.h"
