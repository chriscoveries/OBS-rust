/* Precompiled header (BUILD.md) for the libobs-d3d11 unity groups
 * d3d11.core. C++. Every implementation file in them includes this header and
 * nothing else: the includes and macro setup below are theirs, in group and
 * file order. */

#pragma once

/* d3d11-duplicator.cpp */
#include "d3d11-subsystem.hpp"
#include <unordered_map>

/* d3d11-samplerstate.cpp */
#include <graphics/vec4.h>
#include <float.h>

/* d3d11-shader.cpp */
#include "d3d11-shaderprocessor.hpp"
#include <graphics/vec2.h>
#include <graphics/vec3.h>
#include <graphics/matrix3.h>
#include <graphics/matrix4.h>
#include <util/platform.h>
#include <util/util.hpp>

#include <filesystem>
#include <fstream>
#include <d3dcompiler.h>
#include <system_error>

/* d3d11-shaderprocessor.cpp */

#include <sstream>

/* d3d11-subsystem.cpp */
#include <algorithm>
#include <cassert>
#include <cinttypes>
#include <optional>
#include <util/base.h>
#include <util/dstr.h>
#include <winternl.h>
#include <d3d9.h>
#include <shellscalingapi.h>
#include <d3dkmthk.h>
