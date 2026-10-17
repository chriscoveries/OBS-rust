/* Precompiled header (BUILD.md) for the plugins/linux-jack unity groups
 * linux-jack.nix. C. Every implementation file in them includes this header and
 * nothing else: the includes and macro setup below are theirs, in group and
 * file order. */

#pragma once

/* jack-input.c */
#include "jack-wrapper.h"

#include <obs-module.h>
#include <util/dstr.h>

/* jack-wrapper.c */

#include <util/threading.h>
#include <stdio.h>

#include <util/platform.h>
