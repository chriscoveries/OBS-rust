/* Precompiled header (BUILD.md) for the libobs-opengl unity groups
 * opengl.windows. C. Every implementation file in them includes this header and
 * nothing else: the includes and macro setup below are theirs, in group and
 * file order. */

#pragma once

/* gl-windows.c */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <util/darray.h>
#include "gl-subsystem.h"
#include <glad/glad_wgl.h>
