/* Precompiled header (BUILD.md) for the plugins/linux-v4l2 unity groups
 * linux-v4l2.nix. C. Every implementation file in them includes this header and
 * nothing else: the includes and macro setup below are theirs, in group and
 * file order. */

#pragma once

/* linux-v4l2.c */
#include <obs-module.h>
#include <util/platform.h>

/* v4l2-busy.c */
#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <limits.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "v4l2-busy.h"

/* v4l2-controls.c */
#include <fcntl.h>

#include <linux/videodev2.h>
#include <libv4l2.h>

#include "v4l2-controls.h"

/* v4l2-decoder.c */
#include <libavutil/error.h>

#include "v4l2-decoder.h"

/* v4l2-frame-action.c */

#include "v4l2-frame-action.h"

/* v4l2-framerate.c */
#include "v4l2-framerate.h"
#include "v4l2-helpers.h"

/* v4l2-helpers.c */
#include <inttypes.h>

#include <sys/mman.h>

#include <util/bmem.h>

/* v4l2-input-caps.c */

#include "v4l2-input-caps.h"

/* v4l2-input.c */
#include <sys/time.h>
#include <sys/types.h>
#include <sys/ioctl.h>
#include <sys/select.h>

#include <util/threading.h>
#include <util/dstr.h>

#ifdef HAVE_UDEV
#include "v4l2-udev.h"
#endif

/* vcam-format.c */
#include "vcam-format.h"
