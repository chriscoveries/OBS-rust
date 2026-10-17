/* Precompiled header (BUILD.md) for the libobs-opengl unity groups
 * opengl.nix. C. Every implementation file in them includes this header and
 * nothing else: the includes and macro setup below are theirs, in group and
 * file order. */

#pragma once

/* gl-egl-common.c (its DRM types come with the includes) */
#include "gl-egl-common.h"

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <xf86drm.h>

#include <glad/glad_egl.h>

#if defined(__linux__)

#include <linux/types.h>
#include <asm/ioctl.h>
typedef unsigned int drm_handle_t;

#else

#include <stdint.h>
#include <sys/ioccom.h>
#include <sys/types.h>
typedef int8_t __s8;
typedef uint8_t __u8;
typedef int16_t __s16;
typedef uint16_t __u16;
typedef int32_t __s32;
typedef uint32_t __u32;
typedef int64_t __s64;
typedef uint64_t __u64;
typedef size_t __kernel_size_t;
typedef unsigned long drm_handle_t;

#endif

/* gl-nix.c */
#include "gl-nix.h"
#include "gl-x11-egl.h"

#ifdef ENABLE_WAYLAND
#include "gl-wayland-egl.h"
#endif

/* gl-x11-egl.c */
#include <X11/Xlib.h>
#include <X11/Xlib-xcb.h>

#include <xcb/xcb.h>

#include <stdio.h>

#include "gl-egl-common.h"

#include <glad/glad_egl.h>
