/* Precompiled header (BUILD.md) for the shared/file-updater unity groups
 * file-updater.core. C. Every implementation file in them includes this header and
 * nothing else: the includes and macro setup below are theirs, in group and
 * file order. */

#pragma once

/* file-updater/file-updater.c */
#include <util/curl/curl-helper.h>
#include <util/threading.h>
#include <util/platform.h>
#include <util/darray.h>
#include <util/dstr.h>
#include <obs-data.h>
#include "file-updater/file-updater.h"
