cmake_minimum_required(VERSION 3.28...3.30)

add_subdirectory(api)

if(NOT ENABLE_FRONTEND)
  target_disable_feature(obs "User Interface")
  return()
else()
  target_enable_feature(obs "User Interface")
endif()

find_package(FFmpeg 8.0 REQUIRED COMPONENTS avcodec avutil avformat swscale)
find_package(CURL REQUIRED)

if(NOT TARGET OBS::json11)
  add_subdirectory("${CMAKE_SOURCE_DIR}/deps/json11" json11)
endif()

if(NOT TARGET OBS::libobs)
  add_subdirectory("${CMAKE_SOURCE_DIR}/libobs" libobs)
endif()

if(NOT TARGET OBS::bpm)
  add_subdirectory("${CMAKE_SOURCE_DIR}/shared/bpm" bpm)
endif()

if(NOT TARGET OBS::qt-obs-spinbox)
  add_subdirectory("${CMAKE_SOURCE_DIR}/shared/qt/obs-spinbox" obs-spinbox)
endif()

add_executable(obs-studio)
add_executable(OBS::studio ALIAS obs-studio)

if(TARGET obs-preflight-rust)
  target_link_libraries(obs-studio PRIVATE "${OBS_PREFLIGHT_ARCHIVE}" Threads::Threads ${CMAKE_DL_LIBS})
  add_dependencies(obs-studio obs-preflight-rust)
  target_compile_definitions(obs-studio PRIVATE OBS_PREFLIGHT_SUPPORTED=1)
endif()

if(ENABLE_UNIT_TESTS)
  add_executable(test_virtual_cam_shutdown tests/test_virtual_cam_shutdown.cpp)
  add_test(NAME test_virtual_cam_shutdown COMMAND test_virtual_cam_shutdown)
  add_executable(test_preflight_test_mode tests/test_preflight_test_mode.cpp preflight/preflight-test-mode.cpp)
  target_include_directories(test_preflight_test_mode PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/preflight")
  add_test(NAME test_preflight_test_mode COMMAND test_preflight_test_mode)
  add_executable(
    test_preflight_cloudflare_speed_test
    tests/test_preflight_cloudflare_speed_test.cpp
    preflight/preflight-cloudflare-speed-test.cpp
  )
  target_link_libraries(test_preflight_cloudflare_speed_test PRIVATE CURL::libcurl)
  add_test(NAME test_preflight_cloudflare_speed_test COMMAND test_preflight_cloudflare_speed_test)
  add_executable(test_preflight_loupe tests/test_preflight_loupe.cpp preflight/preflight-loupe-core.cpp)
  target_link_libraries(test_preflight_loupe PRIVATE FFmpeg::avcodec FFmpeg::avutil FFmpeg::swscale)
  if(OS_MACOS)
    get_filename_component(_preflight_loupe_ffmpeg_lib_dir "${FFmpeg_avcodec_LIBRARY}" DIRECTORY)
    target_link_options(test_preflight_loupe PRIVATE "LINKER:-rpath,${_preflight_loupe_ffmpeg_lib_dir}")
    set_property(TARGET test_preflight_loupe PROPERTY XCODE_ATTRIBUTE_ENABLE_HARDENED_RUNTIME NO)
  endif()
  add_test(NAME test_preflight_loupe COMMAND test_preflight_loupe)
  if(TARGET obs-preflight-rust)
    if(NOT TARGET OBS::properties-view)
      add_subdirectory("${CMAKE_SOURCE_DIR}/shared/properties-view" "${CMAKE_BINARY_DIR}/shared/properties-view")
    endif()
    add_executable(
      test_preflight_ui_render
      tests/test_preflight_ui_render.cpp
      OBSApp_Themes.cpp
      preflight/obs-preflight-collector.c
      preflight/preflight-apply-plan.cpp
      preflight/preflight-bench-window.cpp
      preflight/preflight-loupe-capture.cpp
      preflight/preflight-loupe-core.cpp
      preflight/preflight-cloudflare-speed-test.cpp
      preflight/preflight-test-mode.cpp
      utility/OBSProxyStyle.cpp
      utility/OBSProxyStyle.hpp
    )
    set_target_properties(
      test_preflight_ui_render
      PROPERTIES AUTOMOC ON AUTOUIC ON AUTORCC ON
    )
    target_include_directories(
      test_preflight_ui_render
      PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/preflight"
        "${CMAKE_CURRENT_BINARY_DIR}/test_preflight_ui_render_autogen/include_$<CONFIG>"
    )
    target_compile_definitions(
      test_preflight_ui_render
      PRIVATE
        PREFLIGHT_UI_RENDER_TEST=1
        PREFLIGHT_THEME_RENDER_TEST=1
        PREFLIGHT_GOLDEN_DIR=\"${CMAKE_SOURCE_DIR}/rust/obs-preflight/tests/golden\"
        PREFLIGHT_THEME_DIR=\"${CMAKE_SOURCE_DIR}/frontend/data/themes\"
    )
    target_link_libraries(
      test_preflight_ui_render
      PRIVATE
        "${OBS_PREFLIGHT_ARCHIVE}"
        Threads::Threads
        ${CMAKE_DL_LIBS}
        FFmpeg::avcodec
        FFmpeg::avutil
        FFmpeg::swscale
        OBS::frontend-api
        OBS::libobs
        OBS::properties-view
        CURL::libcurl
    )
    add_dependencies(test_preflight_ui_render obs-preflight-rust)
    if(OS_MACOS)
      get_filename_component(_preflight_ui_ffmpeg_lib_dir "${FFmpeg_avcodec_LIBRARY}" DIRECTORY)
      get_filename_component(_preflight_ui_qt_lib_dir "${Qt6Core_DIR}/../.." ABSOLUTE)
      target_link_options(
        test_preflight_ui_render
        PRIVATE
          "LINKER:-rpath,${_preflight_ui_ffmpeg_lib_dir}"
          "LINKER:-rpath,${_preflight_ui_qt_lib_dir}"
          "LINKER:-rpath,${CMAKE_BINARY_DIR}/libobs/$<CONFIG>"
          "LINKER:-rpath,${CMAKE_BINARY_DIR}/frontend/api/$<CONFIG>"
      )
      set_property(TARGET test_preflight_ui_render PROPERTY XCODE_ATTRIBUTE_ENABLE_HARDENED_RUNTIME NO)
    endif()
    add_test(NAME test_preflight_ui_render COMMAND test_preflight_ui_render)
    set_tests_properties(test_preflight_ui_render PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_SCALE_FACTOR=2")
  endif()
  add_executable(test_config_migration tests/test_config_migration.cpp)
  target_include_directories(test_config_migration PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
  foreach(
    mode
    IN
    ITEMS first existing absent custom empty-custom failure
  )
    add_test(
      NAME test_config_migration_${mode}
      COMMAND test_config_migration "${CMAKE_CURRENT_BINARY_DIR}/config-migration-${mode}" ${mode}
    )
  endforeach()
endif()

target_link_libraries(
  obs-studio
  PRIVATE
    CURL::libcurl
    FFmpeg::avcodec
    FFmpeg::avutil
    FFmpeg::avformat
    FFmpeg::swscale
    OBS::libobs
    OBS::frontend-api
    OBS::json11
    OBS::bpm
    OBS::qt-obs-spinbox
)

include(cmake/ui-components.cmake)
include(cmake/ui-dialogs.cmake)
include(cmake/ui-docks.cmake)
include(cmake/feature-importers.cmake)
include(cmake/feature-plugin-manager.cmake)
include(cmake/ui-models.cmake)
include(cmake/ui-oauth.cmake)
include(cmake/feature-browserpanels.cmake)
include(cmake/ui-qt.cmake)
include(cmake/ui-settings.cmake)
include(cmake/ui-utility.cmake)
include(cmake/ui-widgets.cmake)
include(cmake/ui-wizards.cmake)

if(NOT OAUTH_BASE_URL)
  set(OAUTH_BASE_URL "https://auth.obsproject.com/" CACHE STRING "Default OAuth base URL")
  mark_as_advanced(OAUTH_BASE_URL)
endif()
include(cmake/feature-twitch.cmake)
include(cmake/feature-restream.cmake)
include(cmake/feature-youtube.cmake)
include(cmake/feature-whatsnew.cmake)
include(cmake/feature-idian-playground.cmake)

configure_file(cmake/templates/ui-config.h.in ui-config.h)

target_sources(
  obs-studio
  PRIVATE obs-main.cpp OBSStudioAPI.cpp OBSStudioAPI.hpp OBSApp.cpp OBSApp.hpp OBSApp_Themes.cpp ui-config.h
)

# The Rust build names itself OBS-Studio-Rust and shows its own version, read from
# the libobs-rust crate, next to the upstream OBS version it is based on.
if(ENABLE_RUST_LIBOBS)
  file(STRINGS "${CMAKE_SOURCE_DIR}/rust/libobs-rust/Cargo.toml" _obs_rust_version_line REGEX "^version = \"[^\"]+\"")
  string(REGEX REPLACE "^version = \"([^\"]+)\"" "\\1" OBS_RUST_VERSION "${_obs_rust_version_line}")
  # CI release builds embed the full release string instead (#631).
  if(OBS_RUST_RELEASE_VERSION)
    set(OBS_RUST_VERSION "${OBS_RUST_RELEASE_VERSION}")
  endif()
  if(NOT OBS_RUST_VERSION)
    message(FATAL_ERROR "Could not read the version from rust/libobs-rust/Cargo.toml")
  endif()
  message(STATUS "OBS-Studio-Rust version: ${OBS_RUST_VERSION}")
  target_compile_definitions(obs-studio PRIVATE OBS_RUST_VERSION="${OBS_RUST_VERSION}")
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

foreach(graphics_library IN ITEMS opengl metal d3d11)
  string(TOUPPER ${graphics_library} graphics_library_U)
  if(TARGET OBS::libobs-${graphics_library})
    target_compile_definitions(
      obs-studio
      PRIVATE
        DL_${graphics_library_U}="$<$<IF:$<PLATFORM_ID:Windows>,TARGET_FILE_NAME,TARGET_SONAME_FILE_NAME>:OBS::libobs-${graphics_library}>"
    )
  else()
    target_compile_definitions(obs-studio PRIVATE DL_${graphics_library_U}="")
  endif()
endforeach()

get_target_property(target_sources obs-studio SOURCES)
set(target_cpp_sources ${target_sources})
set(target_hpp_sources ${target_sources})
set(target_qt_sources ${target_sources})
list(FILTER target_cpp_sources INCLUDE REGEX ".+\\.(cpp|mm|c|m)")
list(SORT target_cpp_sources COMPARE NATURAL CASE SENSITIVE ORDER ASCENDING)
list(FILTER target_hpp_sources INCLUDE REGEX ".+\\.(hpp|h)")
list(SORT target_hpp_sources COMPARE NATURAL CASE SENSITIVE ORDER ASCENDING)
list(FILTER target_qt_sources INCLUDE REGEX ".+\\.(ui|qrc)")
list(SORT target_qt_sources COMPARE NATURAL CASE SENSITIVE ORDER ASCENDING)
source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}" PREFIX "Source Files" FILES ${target_cpp_sources})
source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}" PREFIX "Header Files" FILES ${target_hpp_sources})
source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}" PREFIX "Qt Files" FILES ${target_qt_sources})

set_target_properties_obs(obs-studio PROPERTIES FOLDER frontend OUTPUT_NAME "$<IF:$<PLATFORM_ID:Windows>,obs64,${OBS_UNIX_GUI_NAME}>")
