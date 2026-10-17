/* Precompiled header (BUILD.md) for the shared/happy-eyeballs unity groups
 * happy-eyeballs.core. C. Every implementation file in them includes this header and
 * nothing else: the includes and macro setup below are theirs, in group and
 * file order. */

#pragma once

/* happy-eyeballs.c */
#include "happy-eyeballs.h"
#include "util/darray.h"
#include "util/dstr.h"
#include "util/platform.h"
#include "util/threading.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Winsock or BSD sockets: hosts/<os>/, picked by CMake. */
#include "happy-eyeballs-platform.h"
