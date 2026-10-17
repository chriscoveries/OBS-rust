cmake_minimum_required(VERSION 3.28...3.30)

add_library(libobs-winrt-headers INTERFACE)
add_library(OBS::winrt-headers ALIAS libobs-winrt-headers)

target_sources(libobs-winrt-headers INTERFACE winrt-capture.h)
target_include_directories(libobs-winrt-headers INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}")

add_library(libobs-winrt MODULE)
add_library(OBS::libobs-winrt ALIAS libobs-winrt)

# Unity group (BUILD.md): build/winrt.core+.hpp.cpp #includes the
# implementation files behind build/winrt.pch.hpp, which now also holds the
# WinRT headers this target used to precompile through target_precompile_headers.
target_sources(libobs-winrt PRIVATE build/winrt.core+.hpp.cpp winrt-capture.h winrt-dispatch.h)
target_link_libraries(libobs-winrt PRIVATE OBS::libobs OBS::COMutils Dwmapi windowsapp)

# FIXME: WinRT library headers trigger deprecation warning until OBS Studio uses C++20 or newer
target_compile_definitions(libobs-winrt PRIVATE _SILENCE_EXPERIMENTAL_COROUTINE_DEPRECATION_WARNINGS)

set_target_properties_obs(libobs-winrt PROPERTIES FOLDER core)
