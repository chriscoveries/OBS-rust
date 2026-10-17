/* Precompiled header (BUILD.md) for the plugins/obs-outputs unity groups
 * obs-outputs.net-if. C. Every implementation file in them includes this header and
 * nothing else: the includes and macro setup below are theirs, in group and
 * file order. */

#pragma once

/* net-if.c */
#define _WINSOCK_DEPRECATED_NO_WARNINGS

#include "../net-if.h"
#include <util/platform.h>
#include <util/dstr.h>
