/* Precompiled header (BUILD.md) for the plugins/text-freetype2 unity groups
 * text-freetype2.core. C. Every implementation file in them includes this header and
 * nothing else: the includes and macro setup below are theirs, in group and
 * file order. */

#pragma once

/* glyph-atlas.c */
#include "glyph-atlas.h"

/* obs-convenience.c */
#include <obs-module.h>
#include <graphics/vec2.h>
#include <graphics/vec3.h>
#include <graphics/vec4.h>
#include "obs-convenience.h"

/* text-file.c */
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

#include <util/bmem.h>
#include <util/platform.h>

#include "text-file.h"

/* text-freetype2.c */
#include "text-freetype2-host.h"
#include <ft2build.h>
#include FT_FREETYPE_H
#include "text-freetype2.h"
#include "find-font.h"
