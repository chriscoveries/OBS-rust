# OBS CMake precompiled header module
#
# One unified precompiled header per target and language (cmake/pch/), so the
# C standard library, the libobs headers and, for UI targets, all of
# QtCore/QtGui/QtWidgets are parsed once per target instead of once per
# source. Applied to every first-party target at the end of the top-level
# CMakeLists.txt (_obs_build_speed_all).

include_guard(GLOBAL)

option(ENABLE_PCH "Compile each target against one unified precompiled header" ON)

set(_OBS_PCH_DIR "${CMAKE_CURRENT_LIST_DIR}/../pch")

# Sources that #define a feature macro the headers depend on (_GNU_SOURCE,
# _FILE_OFFSET_BITS, WIN32_LEAN_AND_MEAN, ...). A force-included header would
# be seen before that definition, so they compile without the PCH. Defining
# such a macro in the PCH instead is not equivalent: with glibc 2.43,
# _GNU_SOURCE makes strstr() return const char * for a const argument
# (librtmp's parseurl.c then fails with -Werror=discarded-qualifiers).
#
# Likewise the sources that need the full <windows.h> (winsock's u_long):
# on Windows arm64 the PCH's libobs headers (util/sse-intrin.h, then SIMDe's
# x86/sse.h) include it with WIN32_LEAN_AND_MEAN first, making their own
# include a no-op.
set(
  _OBS_PCH_SKIP_SOURCES
  plugins/obs-outputs/librtmp/rtmp.c
  plugins/obs-browser/browser-app.cpp
  plugins/obs-browser/obs-browser-page/obs-browser-page-main.cpp
)

# _obs_pch_profile: pick the header set from what <target> links.
function(_obs_pch_profile target out)
  get_target_property(_explicit ${target} OBS_PCH_PROFILE)
  if(_explicit)
    set(${out} ${_explicit} PARENT_SCOPE)
    return()
  endif()
  if(target STREQUAL obs-studio)
    set(${out} frontend PARENT_SCOPE)
    return()
  endif()
  if(target STREQUAL libobs-objects)
    set(${out} libobs PARENT_SCOPE)
    return()
  endif()
  get_target_property(_links ${target} LINK_LIBRARIES)
  set(_qt FALSE)
  set(_obs FALSE)
  foreach(_link IN LISTS _links)
    if(_link MATCHES "^(Qt6?::Widgets|Qt::Widgets)$")
      set(_qt TRUE)
    elseif(_link MATCHES "^(OBS::libobs|libobs)$")
      set(_obs TRUE)
    endif()
  endforeach()
  if(_qt AND _obs)
    set(${out} qt PARENT_SCOPE)
  elseif(_qt)
    set(${out} qt-std PARENT_SCOPE)
  elseif(_obs)
    set(${out} plugin PARENT_SCOPE)
  else()
    set(${out} std PARENT_SCOPE)
  endif()
endfunction()

# obs_target_pch: give <target> its unified precompiled header(s).
function(obs_target_pch target)
  if(NOT ENABLE_PCH)
    return()
  endif()
  get_target_property(_type ${target} TYPE)
  if(_type MATCHES "^(INTERFACE_LIBRARY|UTILITY)$")
    return()
  endif()
  get_target_property(_disabled ${target} OBS_DISABLE_PCH)
  if(_disabled)
    return()
  endif()

  get_target_property(_sources ${target} SOURCES)
  set(_has_c FALSE)
  set(_has_cxx FALSE)
  set(_has_objc FALSE)
  foreach(_source IN LISTS _sources)
    if(_source MATCHES "\\.c(>|$)")
      set(_has_c TRUE)
    elseif(_source MATCHES "\\.(cpp|cc|cxx)(>|$)")
      set(_has_cxx TRUE)
    elseif(_source MATCHES "\\.m(>|$)")
      set(_has_objc TRUE)
    endif()
  endforeach()

  # Xcode gives a target one prefix header for all its sources, whatever their
  # language: a C++ header would be force-included into its C and Objective-C
  # sources too. Targets that mix them compile without a PCH there.
  if(CMAKE_GENERATOR STREQUAL "Xcode" AND _has_cxx AND (_has_c OR _has_objc))
    return()
  endif()

  _obs_pch_profile(${target} _profile)
  set(_c_header "${_OBS_PCH_DIR}/obs-c-std.h")
  set(_cxx_header "${_OBS_PCH_DIR}/obs-cxx-std.hpp")
  if(_profile STREQUAL libobs)
    set(_c_header "${_OBS_PCH_DIR}/obs-c-libobs.h")
  elseif(_profile MATCHES "^(plugin|qt|frontend)$")
    set(_c_header "${_OBS_PCH_DIR}/obs-c-plugin.h")
    set(_cxx_header "${_OBS_PCH_DIR}/obs-cxx-${_profile}.hpp")
  elseif(_profile STREQUAL qt-std)
    set(_cxx_header "${_OBS_PCH_DIR}/obs-cxx-qt-std.hpp")
  endif()

  if(_has_c)
    target_precompile_headers(${target} PRIVATE "$<$<COMPILE_LANGUAGE:C>:${_c_header}>")
  endif()
  if(_has_cxx)
    target_precompile_headers(${target} PRIVATE "$<$<COMPILE_LANGUAGE:CXX>:${_cxx_header}>")
  endif()

  _obs_target_sources_matching(${target} _skipped ${_OBS_PCH_SKIP_SOURCES})
  # Third-party sources (deps/) that reach a first-party target compile as
  # their authors wrote them, without the force-included header.
  _obs_target_sources_where(${target} _third_party "^${CMAKE_SOURCE_DIR}/deps/")
  list(APPEND _skipped ${_third_party})
  if(_skipped)
    set_source_files_properties(${_skipped} TARGET_DIRECTORY ${target} PROPERTIES SKIP_PRECOMPILE_HEADERS ON)
  endif()

  _obs_pch_ccache(${target})
endfunction()

# _obs_pch_ccache: keep ccache caching sources compiled against a precompiled
# header. ccache refuses them unless sloppiness includes pch_defines and
# time_macros (and, for GCC, the compiler gets -fpch-preprocess), so a
# ccache launcher is wrapped to add those to whatever sloppiness is
# configured already.
function(_obs_pch_ccache target)
  foreach(_lang IN ITEMS C CXX)
    get_target_property(_launcher ${target} ${_lang}_COMPILER_LAUNCHER)
    if(NOT _launcher OR NOT _launcher MATCHES "(^|/|;)ccache(\\.exe)?$" OR _launcher MATCHES "CCACHE_SLOPPINESS")
      continue()
    endif()
    if(NOT DEFINED _OBS_PCH_CCACHE_SLOPPINESS)
      list(GET _launcher -1 _ccache)
      execute_process(
        COMMAND "${_ccache}" --get-config sloppiness
        OUTPUT_VARIABLE _configured
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
      )
      string(REPLACE "," ";" _sloppiness "${_configured},$ENV{CCACHE_SLOPPINESS},pch_defines,time_macros")
      list(REMOVE_ITEM _sloppiness "")
      list(REMOVE_DUPLICATES _sloppiness)
      list(JOIN _sloppiness "," _OBS_PCH_CCACHE_SLOPPINESS)
      set(_OBS_PCH_CCACHE_SLOPPINESS "${_OBS_PCH_CCACHE_SLOPPINESS}" CACHE INTERNAL "")
    endif()
    set_property(
      TARGET ${target}
      PROPERTY
        ${_lang}_COMPILER_LAUNCHER
          "${CMAKE_COMMAND}"
          -E
          env
          "CCACHE_SLOPPINESS=${_OBS_PCH_CCACHE_SLOPPINESS}"
          ${_launcher}
    )
    if(CMAKE_${_lang}_COMPILER_ID STREQUAL "GNU")
      target_compile_options(${target} PRIVATE "$<$<COMPILE_LANGUAGE:${_lang}>:-fpch-preprocess>")
    endif()
  endforeach()
endfunction()

# _obs_target_sources_matching: absolute paths of <target>'s sources that end
# in one of the given repository-relative paths. Sources added through
# generator expressions ($<$<BOOL:...>:file.c>) are matched by the file names
# inside them.
function(_obs_target_all_sources target out)
  get_target_property(_sources ${target} SOURCES)
  get_target_property(_source_dir ${target} SOURCE_DIR)
  # INTERFACE libraries (media-playback, libdshowcapture, ...) hand their
  # sources to the targets that link them; those compile as this target's.
  get_target_property(_links ${target} LINK_LIBRARIES)
  foreach(_link IN LISTS _links)
    if(TARGET "${_link}")
      get_target_property(_interface_sources "${_link}" INTERFACE_SOURCES)
      if(_interface_sources)
        list(APPEND _sources ${_interface_sources})
      endif()
    endif()
  endforeach()
  set(_all "")
  foreach(_source IN LISTS _sources)
    string(REGEX MATCHALL "[^:<>$,;]+\\.(cpp|cxx|cc|mm|m|c)" _files "${_source}")
    foreach(_file IN LISTS _files)
      cmake_path(ABSOLUTE_PATH _file BASE_DIRECTORY "${_source_dir}" NORMALIZE OUTPUT_VARIABLE _absolute)
      list(APPEND _all "${_absolute}")
    endforeach()
  endforeach()
  set(${out} ${_all} PARENT_SCOPE)
endfunction()

function(_obs_target_sources_matching target out)
  _obs_target_all_sources(${target} _all)
  set(_matches "")
  foreach(_absolute IN LISTS _all)
    foreach(_wanted IN LISTS ARGN)
      if(_absolute MATCHES "/${_wanted}$")
        list(APPEND _matches "${_absolute}")
      endif()
    endforeach()
  endforeach()
  set(${out} ${_matches} PARENT_SCOPE)
endfunction()

# _obs_target_sources_where: absolute paths of <target>'s sources (its own and
# those INTERFACE libraries hand it) whose path matches <regex>.
function(_obs_target_sources_where target out regex)
  _obs_target_all_sources(${target} _all)
  set(_matches "")
  foreach(_absolute IN LISTS _all)
    if(_absolute MATCHES "${regex}")
      list(APPEND _matches "${_absolute}")
    endif()
  endforeach()
  set(${out} ${_matches} PARENT_SCOPE)
endfunction()

# _obs_target_build_speed: the PCH and unity settings for one target.
function(_obs_target_build_speed target)
  get_target_property(_imported ${target} IMPORTED)
  get_target_property(_source_dir ${target} SOURCE_DIR)
  if(_imported OR NOT _source_dir)
    return()
  endif()
  # Implementation files (BUILD.md) compile in their language wherever a
  # target lists them directly, the C tests included.
  _obs_target_impl_languages(${target})
  # Third-party code (deps/), the unchanged C tests (test/) and Corrosion's
  # Cargo targets keep their own build.
  cmake_path(RELATIVE_PATH _source_dir BASE_DIRECTORY "${CMAKE_SOURCE_DIR}" OUTPUT_VARIABLE _relative)
  if(_relative MATCHES "^(deps|test|rust)(/|$)" OR target MATCHES "^(_cargo|cargo-|corrosion)")
    return()
  endif()
  # Converted components (BUILD.md) bring their own groups and headers.
  get_target_property(_grouped ${target} OBS_GROUPED)
  if(_grouped)
    return()
  endif()
  obs_target_pch(${target})
  obs_target_unity(${target})
endfunction()

# _obs_build_speed_all: apply the settings to every target of <directory> and
# its subdirectories. Runs once, deferred to the end of the top-level
# CMakeLists.txt, when every target and its sources exist.
function(_obs_build_speed_all directory)
  get_property(_targets DIRECTORY "${directory}" PROPERTY BUILDSYSTEM_TARGETS)
  foreach(_target IN LISTS _targets)
    _obs_target_build_speed(${_target})
  endforeach()
  get_property(_subdirectories DIRECTORY "${directory}" PROPERTY SUBDIRECTORIES)
  foreach(_subdirectory IN LISTS _subdirectories)
    _obs_build_speed_all("${_subdirectory}")
  endforeach()
endfunction()

# _obs_build_speed_run: unity groups first (BUILD.md; they record which
# target owns each implementation file), then the per-target settings.
function(_obs_build_speed_run directory)
  _obs_groups_all("${directory}")
  _obs_build_speed_all("${directory}")
endfunction()

if(NOT _OBS_BUILD_SPEED_DEFERRED)
  set(_OBS_BUILD_SPEED_DEFERRED TRUE)
  cmake_language(DEFER DIRECTORY "${CMAKE_SOURCE_DIR}" CALL _obs_build_speed_run "${CMAKE_SOURCE_DIR}")
endif()
