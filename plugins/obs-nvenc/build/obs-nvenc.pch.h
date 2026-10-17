/* Precompiled header (BUILD.md) for the plugins/obs-nvenc unity groups
 * obs-nvenc.core, obs-nvenc.windows, obs-nvenc.linux, obs-nvenc.cuda-load, obs-nvenc.cuda-load-rust. C. Every implementation file in them includes this header and
 * nothing else: the includes and macro setup below are theirs, in group and
 * file order. */

#pragma once

/* cuda-helpers.c */
#include "../obs-nvenc.h"

#include "../nvenc-internal.h"
#include "../cuda-helpers.h"

#include <util/platform.h>
#include <util/threading.h>
#include <util/config-file.h>
#include <util/dstr.h>
#include <util/pipe.h>

/* nvenc-compat.c */
#include "../nvenc-helpers.h"

/* nvenc-opts-parser.c */

#include <stdio.h>
#include <inttypes.h>

/* nvenc.c */
#include "../nvenc-override.h"

#include <obs-nal.h>
#include <util/darray.h>

/* obs-nvenc.c */
#include <obs-module.h>
