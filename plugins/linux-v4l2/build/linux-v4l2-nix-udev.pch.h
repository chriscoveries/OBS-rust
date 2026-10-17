/* Precompiled header (BUILD.md) for the plugins/linux-v4l2 unity groups
 * linux-v4l2.nix-udev. C. Every implementation file in them includes this header and
 * nothing else: the includes and macro setup below are theirs, in group and
 * file order. */

#pragma once

/* v4l2-udev.c */
#include <sys/eventfd.h>
#include <poll.h>
#include <unistd.h>
#include <libudev.h>

#include <util/threading.h>
#include <util/darray.h>
#include <obs.h>

#include "v4l2-udev.h"
