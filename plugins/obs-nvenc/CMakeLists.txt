cmake_minimum_required(VERSION 3.28...3.30)

option(ENABLE_NVENC "Build NVIDIA Hardware Encoder Plugin" ON)
option(ENABLE_NVENC_FFMPEG_IDS "Register FFmpeg encoder IDs" ON)
mark_as_advanced(ENABLE_NVENC_FFMPEG_IDS)

if(NOT ENABLE_NVENC)
  target_disable_feature(obs-nvenc "NVIDIA Hardware Encoder")
  return()
endif()

if(NOT TARGET OBS::opts-parser)
  add_subdirectory("${CMAKE_SOURCE_DIR}/shared/opts-parser" "${CMAKE_BINARY_DIR}/shared/opts-parser")
endif()

if(OS_LINUX AND NOT TARGET OBS::glad)
  add_subdirectory("${CMAKE_SOURCE_DIR}/deps/glad" "${CMAKE_BINARY_DIR}/deps/glad")
endif()

find_package(FFnvcodec 12 REQUIRED)

add_library(obs-nvenc MODULE)
add_library(OBS::nvenc ALIAS obs-nvenc)

add_subdirectory(obs-nvenc-test)

set(
  _obs_nvenc_sources
  $<$<PLATFORM_ID:Linux>:build/obs-nvenc.linux+.h.c>
  $<$<PLATFORM_ID:Windows>:build/obs-nvenc.windows+.h.c>
  build/obs-nvenc.core+.h.c
  build/obs-nvenc.override+.h.c
  cuda-helpers.h
  nvenc-helpers.h
  nvenc-internal.h
  nvenc-override.h
  obs-nvenc.h
)

# obs-nvenc, the plugin that installs, uses the C CUDA driver loader
# (cuda-load.c.h). obs-nvenc-with-rust is the same plugin with the Rust loader
# (cuda-load-rust.c.h + rust/obs-nvenc-plugin), built whenever libobs's Rust twin
# is (#1065) and never installed or loaded.
set(_obs_nvenc_targets obs-nvenc)
target_sources(obs-nvenc PRIVATE ${_obs_nvenc_sources} build/obs-nvenc.cuda-load+.h.c)
if(TARGET libobs-with-rust)
  add_library(obs-nvenc-with-rust MODULE ${_obs_nvenc_sources} build/obs-nvenc.cuda-load-rust+.h.c)
  list(APPEND _obs_nvenc_targets obs-nvenc-with-rust)
  # The CUDA driver loader (rust/obs-nvenc, via rust/obs-nvenc-plugin) is linked into the plugin, not
  # re-exported through libobs-rust: libobs links exactly one Rust staticlib
  # and its exported symbols must stay the C build's set (libobs/cmake/rust.cmake),
  # and nothing but this plugin calls obs_nvenc_cuda_fill. Each module carries its
  # own copy of std, so keep the Rust symbols out of the module's dynamic table.
  # Corrosion is fetched by libobs/cmake/rust.cmake.
  corrosion_import_crate(
    MANIFEST_PATH "${CMAKE_SOURCE_DIR}/Cargo.toml"
    CRATES obs-nvenc-plugin
    CRATE_TYPES staticlib
    PROFILE release
  )
  target_link_libraries(obs-nvenc-with-rust PRIVATE obs_nvenc_plugin-static)
  if(OS_LINUX)
    target_link_options(obs-nvenc-with-rust PRIVATE "LINKER:--exclude-libs,ALL")
  endif()
  set_target_properties(
    obs-nvenc-with-rust
    PROPERTIES
      FOLDER plugins/obs-nvenc
      PREFIX ""
      OUTPUT_NAME obs-nvenc
      LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/obs-nvenc-with-rust"
  )
endif()

foreach(target IN LISTS _obs_nvenc_targets)
  target_link_libraries(
    ${target}
    PRIVATE OBS::libobs OBS::opts-parser FFnvcodec::FFnvcodec $<$<PLATFORM_ID:Linux>:OBS::glad>
  )
  target_compile_definitions(${target} PRIVATE $<$<BOOL:${ENABLE_NVENC_FFMPEG_IDS}>:REGISTER_FFMPEG_IDS>)
endforeach()

if(OS_WINDOWS)
  configure_file(cmake/windows/obs-module.rc.in obs-nvenc.rc)
  target_sources(obs-nvenc PRIVATE obs-nvenc.rc)
endif()

set_target_properties_obs(obs-nvenc PROPERTIES FOLDER plugins/obs-nvenc PREFIX "")
