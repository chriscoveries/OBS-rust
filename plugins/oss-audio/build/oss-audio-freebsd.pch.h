/* Precompiled header (BUILD.md) for the plugins/oss-audio unity groups
 * oss-audio.freebsd. C. Every implementation file in them includes this header and
 * nothing else: the includes and macro setup below are theirs, in group and
 * file order. */

#pragma once

/* oss-input-freebsd.c */
#define _GNU_SOURCE /* getline, asprintf, strndup on glibc (the Linux test) */

#include <ctype.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "oss-input-platform.h"
