/* Precompiled header (BUILD.md) for the plugins/linux-pipewire unity groups
 * linux-pipewire.nix-camera. C. Every implementation file in them includes this header and
 * nothing else: the includes and macro setup below are theirs, in group and
 * file order. */

#pragma once

/* camera-portal.c */
#include "pipewire.h"

#include "formats.h"
#include "portal.h"

#include <util/dstr.h>

#include <fcntl.h>
#include <unistd.h>
#include <gio/gio.h>
#include <gio/gunixfdlist.h>
#include <spa/node/keys.h>
#include <spa/pod/iter.h>
#include <spa/pod/parser.h>
#include <spa/param/props.h>
#include <spa/utils/defs.h>
#include <spa/utils/keys.h>
#include <spa/utils/result.h>
