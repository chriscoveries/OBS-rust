/* Precompiled header (BUILD.md) for the plugins/linux-capture unity groups
 * linux-capture.nix. C. Every implementation file in them includes this header and
 * nothing else: the includes and macro setup below are theirs, in group and
 * file order. */

#pragma once

/* linux-capture.c */
#include <obs-module.h>
#include <obs-nix-platform.h>
#include "xcomposite-input.h"

/* xcomposite-input.c */
#include <glad/glad.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/Xlib-xcb.h>
#include <xcb/xcb.h>
#include <xcb/composite.h>
#include <pthread.h>

#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <ctype.h>

#include "xhelpers.h"
#include "xcursor-xcb.h"
#include <util/platform.h>
#include <util/dstr.h>
#include <util/darray.h>

/* xcursor-xcb.c */
#include <xcb/xfixes.h>

#include <util/bmem.h>

/* xhelpers.c */
#include <sys/shm.h>
#include <xcb/randr.h>
#include <xcb/xinerama.h>

/* xshm-input.c */
#include <inttypes.h>
#include <xcb/shm.h>
