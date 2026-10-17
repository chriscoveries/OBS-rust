/* Precompiled header (BUILD.md) for the plugins/text-freetype2 unity groups
 * text-freetype2.nix. C. Every implementation file in them includes this header and
 * nothing else: the includes and macro setup below are theirs, in group and
 * file order. */

#pragma once

/* find-font-unix.c */
#include <fontconfig/fontconfig.h>

#include <util/base.h>
#include <util/dstr.h>

#include "find-font.h"
#include "text-freetype2.h"

/* text-freetype2-host-posix.c */
#include <obs-module.h>
#include "text-freetype2-host.h"
