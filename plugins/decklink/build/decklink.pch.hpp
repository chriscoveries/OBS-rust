/* Precompiled header (BUILD.md) for the plugins/decklink unity groups
 * decklink.core. C++. Every implementation file in them includes this header and
 * nothing else: the includes and macro setup below are theirs, in group and
 * file order. */

#pragma once

/* DecklinkBase.cpp */
#include "../DecklinkBase.h"

/* DecklinkInput.cpp */
#include "../DecklinkInput.hpp"

#include <util/threading.h>

/* DecklinkOutput.cpp */
#include "../DecklinkOutput.hpp"

/* OBSVideoFrame.cpp */
#include "../OBSVideoFrame.h"

/* decklink-device-discovery.cpp */
#include "../decklink-device-discovery.hpp"
#include "../decklink-device.hpp"

/* decklink-device-instance.cpp */
#include "../decklink-device-instance.hpp"
#include "../audio-repack.hpp"

#include <util/platform.h>
#include <util/util_uint64.h>

#include <sstream>
#include <iomanip>
#include <algorithm>

#include <caption/caption.h>
#include <util/bitstream.h>

/* decklink-device-mode.cpp */
#include "../decklink-device-mode.hpp"

/* decklink-devices.cpp */
#include "../decklink-devices.hpp"

/* decklink-output.cpp */
#include <obs-module.h>
#include <obs-avc.h>

#include "../const.h"

#include <media-io/video-scaler.h>

/* decklink-source.cpp */

#include "../util.hpp"
