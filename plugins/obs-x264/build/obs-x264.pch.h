/* Precompiled header (BUILD.md) for the plugins/obs-x264 unity groups
 * obs-x264.core. C. Every implementation file in them includes this header and
 * nothing else: the includes and macro setup below are theirs, in group and
 * file order. */

#pragma once

/* obs-x264-plugin-main.c */
#include <obs-module.h>

/* obs-x264.c */
#include <stdio.h>
#include <string.h>
#include <util/bmem.h>
#include <util/dstr.h>
#include <util/darray.h>
#include <util/platform.h>
#include <util/threading.h>
#include <opts-parser.h>

#ifndef _STDINT_H_INCLUDED
#define _STDINT_H_INCLUDED
#endif

#include <x264.h>
