cmake_minimum_required(VERSION 3.28...3.30)

include(cmake/obs-version.cmake)

if(OS_WINDOWS AND NOT OBS_PARENT_ARCHITECTURE STREQUAL CMAKE_VS_PLATFORM_NAME)
  include(cmake/os-windows.cmake)
  return()
endif()

include(cmake/obs-core-modules.cmake)

find_package(SIMDe REQUIRED)
find_package(Threads REQUIRED)

find_package(FFmpeg 8.0 REQUIRED avformat avutil swscale swresample OPTIONAL_COMPONENTS avcodec)

find_package(ZLIB REQUIRED)
find_package(Uthash REQUIRED)

find_package(jansson REQUIRED)
if(NOT TARGET OBS::caption)
  add_subdirectory("${CMAKE_SOURCE_DIR}/deps/libcaption" "${CMAKE_BINARY_DIR}/deps/libcaption")
endif()

# Two libobs binaries share one compile of every unported source (#1065):
# - libobs: those objects + the C of each ported unit. The one everything links
#   against, installs and packages.
# - libobs-with-rust: those objects + the rust/libobs-rust staticlib in place
#   of the ported C (cmake/rust.cmake). Same file name and SONAME, in its own
#   directory, never installed or linked: Tier 2 runs the unchanged C tests
#   against it by pointing the loader at that directory. Built whenever cargo is
#   available (the Flatpak sandbox has none).
add_library(libobs-objects OBJECT)
add_library(libobs SHARED)
add_library(OBS::libobs ALIAS libobs)

find_program(OBS_LIBOBS_CARGO cargo)
if(OBS_LIBOBS_CARGO)
  add_library(libobs-with-rust SHARED)
  set(LIBOBS_SHARED_TARGETS libobs libobs-with-rust)
else()
  message(WARNING "cargo not found: building libobs without its Rust-linked twin (libobs-with-rust, #1065)")
  set(LIBOBS_SHARED_TARGETS libobs)
endif()

include(cmake/sources.cmake)

set_target_properties(libobs-objects PROPERTIES POSITION_INDEPENDENT_CODE TRUE)
target_sources(libobs-objects PRIVATE ${LIBOBS_SOURCES})
target_sources(libobs PRIVATE ${LIBOBS_PORTED_SOURCES})
# libobs-version and libobs-core-modules are object libraries too, and CMake
# adds an object library's objects only to targets that link it directly, so
# each shared library links them itself (libobs-objects gets their headers).
foreach(target IN LISTS LIBOBS_SHARED_TARGETS)
  target_link_libraries(${target} PRIVATE libobs-objects OBS::libobs-core-modules OBS::libobs-version)
endforeach()

# Temporarily disables deprecation warnings while obs_data_autoselect_* is deprecated.
if(CMAKE_C_COMPILER_ID STREQUAL "MSVC")
  set_source_files_properties(build/libobs.ported-data+.h.c PROPERTIES COMPILE_OPTIONS "/wd4996")
elseif(
  CMAKE_C_COMPILER_ID STREQUAL "GNU"
  OR CMAKE_C_COMPILER_ID STREQUAL "Clang"
  OR CMAKE_C_COMPILER_ID STREQUAL "AppleClang"
)
  set_source_files_properties(build/libobs.ported-data+.h.c PROPERTIES COMPILE_OPTIONS "-Wno-deprecated-declarations")
endif()

# Usage requirements on libobs-objects are PUBLIC so they reach both shared
# libraries' own sources and link lines; the ones plugins also need are
# repeated on libobs's interface below.
target_compile_features(libobs-objects PUBLIC cxx_std_17)
target_compile_features(libobs INTERFACE cxx_std_17)

target_compile_definitions(
  libobs-objects
  PUBLIC
    IS_LIBOBS
    $<BUILD_INTERFACE:$<$<BOOL:${ENABLE_HEVC}>:ENABLE_HEVC>>
    $<BUILD_INTERFACE:$<$<BOOL:${ENABLE_FFMPEG_MUX_DEBUG}>:SHOW_SUBPROCESSES>>
)
target_compile_definitions(
  libobs
  INTERFACE
    $<BUILD_INTERFACE:$<$<BOOL:${ENABLE_HEVC}>:ENABLE_HEVC>>
    $<BUILD_INTERFACE:$<$<BOOL:${ENABLE_FFMPEG_MUX_DEBUG}>:SHOW_SUBPROCESSES>>
)

target_link_libraries(
  libobs-objects
  PUBLIC
    OBS::caption
    OBS::libobs-core-modules
    OBS::libobs-version
    FFmpeg::avcodec
    FFmpeg::avformat
    FFmpeg::avutil
    FFmpeg::swscale
    FFmpeg::swresample
    jansson::jansson
    Uthash::Uthash
    ZLIB::ZLIB
    SIMDe::SIMDe
    Threads::Threads
)
target_link_libraries(libobs INTERFACE SIMDe::SIMDe Threads::Threads)

if(TARGET libobs-with-rust)
  target_sources(libobs-with-rust PRIVATE ${LIBOBS_RUST_SOURCES})
  include(cmake/rust.cmake)
endif()

if(OS_WINDOWS)
  include(cmake/os-windows.cmake)
elseif(OS_MACOS)
  include(cmake/os-macos.cmake)
elseif(OS_LINUX)
  include(cmake/os-linux.cmake)
elseif(OS_FREEBSD OR OS_OPENBSD)
  include(cmake/os-freebsd.cmake)
endif()

configure_file(obsconfig.h.in "${CMAKE_BINARY_DIR}/config/obsconfig.h" @ONLY)

foreach(target libobs-objects libobs)
  target_include_directories(
    ${target}
    PUBLIC "$<BUILD_INTERFACE:${CMAKE_BINARY_DIR}/config>" "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>"
  )
endforeach()

set(
  public_headers
  callback/calldata.h
  callback/decl.h
  callback/proc.h
  callback/signal.h
  graphics/axisang.h
  graphics/bounds.h
  graphics/effect-parser.h
  graphics/effect.h
  graphics/graphics.h
  graphics/image-file.h
  graphics/input.h
  graphics/libnsgif/nsgif.h
  graphics/math-defs.h
  graphics/math-extra.h
  graphics/matrix3.h
  graphics/matrix4.h
  graphics/plane.h
  graphics/quat.h
  graphics/shader-parser.h
  graphics/srgb.h
  graphics/vec2.h
  graphics/vec3.h
  graphics/vec4.h
  media-io/audio-io.h
  media-io/audio-math.h
  media-io/audio-resampler.h
  media-io/format-conversion.h
  media-io/frame-rate.h
  media-io/media-io-defs.h
  media-io/media-remux.h
  media-io/video-frame.h
  media-io/video-io.h
  media-io/video-scaler.h
  obs-audio-controls.h
  obs-avc.h
  obs-config.h
  obs-data.h
  obs-defs.h
  obs-encoder.h
  obs-hotkey.h
  obs-hotkeys.h
  obs-interaction.h
  obs-missing-files.h
  obs-module.h
  obs-nal.h
  obs-nix-platform.h
  obs-output.h
  obs-properties.h
  obs-service.h
  obs-source.h
  obs.h
  obs.hpp
  util/array-serializer.h
  util/base.h
  util/bitstream.h
  util/bmem.h
  util/c99defs.h
  util/cf-lexer.h
  util/cf-parser.h
  util/config-file.h
  util/crc32.h
  util/darray.h
  util/deque.h
  util/dstr.h
  util/dstr.hpp
  util/file-serializer.h
  util/lexer.h
  util/pipe.h
  util/platform.h
  util/profiler.h
  util/profiler.hpp
  util/serializer.h
  util/sse-intrin.h
  util/task.h
  util/text-lookup.h
  util/threading-posix.h
  util/threading.h
  util/uthash.h
  util/util.hpp
  util/util_uint128.h
  util/util_uint64.h
)

if(OS_WINDOWS)
  list(
    APPEND public_headers
    util/threading-windows.h
    util/windows/ComPtr.hpp
    util/windows/CoTaskMemPtr.hpp
    util/windows/device-enum.h
    util/windows/HRError.hpp
    util/windows/win-registry.h
    util/windows/win-version.h
    util/windows/window-helpers.h
    util/windows/WinHandle.hpp
  )
elseif(OS_MACOS)
  list(APPEND public_headers util/apple/cfstring-utils.h)
endif()

if(ENABLE_HEVC)
  list(APPEND public_headers obs-hevc.h)
endif()

set_property(TARGET libobs APPEND PROPERTY OBS_PUBLIC_HEADERS ${public_headers})

set_target_properties_obs(
  libobs
  PROPERTIES FOLDER core
             VERSION 0
             SOVERSION "${OBS_VERSION_MAJOR}"
)

target_export(libobs)

# libobs-with-rust takes libobs's final file name and ABI version, which each
# platform's set_target_properties_obs decides (Linux: SOVERSION 30, not the
# OBS major version), so it loads in libobs's place (#1065).
if(TARGET libobs-with-rust)
  foreach(
    property
    VERSION
    SOVERSION
    OUTPUT_NAME
    PREFIX
    FRAMEWORK
    FRAMEWORK_VERSION
    MACOSX_FRAMEWORK_IDENTIFIER
    INSTALL_NAME_DIR
    BUILD_WITH_INSTALL_NAME_DIR
  )
    get_target_property(value libobs ${property})
    if(property STREQUAL "OUTPUT_NAME" AND value MATCHES "-NOTFOUND$")
      # No OUTPUT_NAME (macOS): the file name is the target name, libobs.
      set(value libobs)
    endif()
    if(NOT value MATCHES "-NOTFOUND$")
      set_property(TARGET libobs-with-rust PROPERTY ${property} "${value}")
    endif()
  endforeach()
endif()
