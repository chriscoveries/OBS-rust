/* Precompiled header (BUILD.md) for the plugins/rtmp-services unity groups
 * rtmp-services.core. C. Every implementation file in them includes this header and
 * nothing else: the includes and macro setup below are theirs, in group and
 * file order. */

#pragma once

/* rtmp-common.c */
#include <util/platform.h>
#include <util/dstr.h>
#include <obs-module.h>
#include <jansson.h>
#include <obs-config.h>

#include "../rtmp-format-ver.h"
#include "../service-specific/twitch.h"
#include "../service-specific/nimotv.h"
#include "../service-specific/showroom.h"
#include "../service-specific/dacast.h"
#include "../service-specific/amazon-ivs.h"

/* rtmp-services-main.c */
#include <util/text-lookup.h>
#include <util/threading.h>
#include <file-updater/file-updater.h>

/* service-specific/amazon-ivs.c */
#include "../service-specific/service-ingest.h"

/* service-specific/nimotv.c */
#include <util/curl/curl-helper.h>
#include <stdlib.h>
#include <string.h>
#include "util/base.h"
#include <time.h>

/* service-specific/showroom.c */
#include <util/darray.h>
