/* Precompiled header (BUILD.md) for the libobs unity group
 * libobs.nix-portal-shortcuts: only built with GIO, so <gio/gio.h> stays out
 * of libobs-wayland.pch.h. C. Its implementation file includes this header and
 * nothing else. */

#pragma once

#include "obs-nix-portal-shortcuts.h"
#include "util/threading.h"

#include <gio/gio.h>
#include <unistd.h>
