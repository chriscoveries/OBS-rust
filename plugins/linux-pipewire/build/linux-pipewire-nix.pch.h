/* Precompiled header (BUILD.md) for the plugins/linux-pipewire unity groups
 * linux-pipewire.nix. C. Every implementation file in them includes this header and
 * nothing else: the includes and macro setup below are theirs, in group and
 * file order. */

#pragma once

/* formats.c */
#include "formats.h"

#include <libdrm/drm_fourcc.h>
#include <pipewire/pipewire.h>

/* frame-layout.c */
#include "frame-layout.h"

#include <spa/utils/defs.h>

/* frame-pacing.c */
#include "frame-pacing.h"

#include <string.h>

/* linux-pipewire.c */
#include <obs-module.h>
#include <obs-nix-platform.h>
#include <glad/glad.h>

#include "screencast-portal.h"

#if PW_CHECK_VERSION(0, 3, 60)
#include "camera-portal.h"
#endif

/* pipewire.c */
#include "pipewire.h"

#include "reconnect-policy.h"

#include <util/darray.h>

#include <gio/gio.h>
#include <gio/gunixfdlist.h>
#include <glib/gstdio.h>

#include <fcntl.h>
#include <spa/param/video/format-utils.h>
#include <spa/buffer/meta.h>
#include <spa/debug/format.h>
#include <spa/debug/types.h>
#include <spa/param/video/type-info.h>
#include <spa/utils/result.h>

/* portal.c */
#include "portal.h"

#include <util/dstr.h>

/* reconnect-policy.c */

#include <errno.h>

#include <pipewire/core.h>

/* screencast-portal.c */

#include <pthread.h>
#include <util/platform.h>
