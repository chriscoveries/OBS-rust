/* Precompiled header (BUILD.md) for the libobs unity groups libobs.ported-macos-cocoa. Objective-C.
 * Every implementation file in them includes this header and nothing else: the includes and macro setup below are
 * theirs, in group and file order. */

#pragma once

/* util/platform-cocoa.m */
#include "util/base.h"
#include "util/platform.h"
#include "util/dstr.h"

#include <dlfcn.h>
#include <time.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/param.h>
#include <sys/sysctl.h>

#include <CoreServices/CoreServices.h>
#include <mach/mach.h>
#include <mach/mach_time.h>
#include <mach-o/dyld.h>

#include <IOKit/pwr_mgt/IOPMLib.h>

#import <Cocoa/Cocoa.h>

#include "util/apple/cfstring-utils.h"
