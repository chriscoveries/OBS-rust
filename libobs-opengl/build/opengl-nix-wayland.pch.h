/* Precompiled header (BUILD.md) for the libobs-opengl unity groups
 * opengl.nix-wayland. C. Every implementation file in them includes this header and
 * nothing else: the includes and macro setup below are theirs, in group and
 * file order. */

#pragma once

/* gl-wayland-egl.c */
#include "gl-wayland-egl.h"

#include <wayland-client.h>
#include <wayland-egl.h>

#include "gl-egl-common.h"
#include "gl-egl-errors.h"

#include <glad/glad_egl.h>
