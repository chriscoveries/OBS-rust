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

target_sources(
  obs-nvenc
  PRIVATE
    $<$<PLATFORM_ID:Linux>:nvenc-opengl.c>
    $<$<PLATFORM_ID:Windows>:nvenc-d3d11.c>
    cuda-helpers.c
    cuda-helpers.h
    $<$<NOT:$<BOOL:${ENABLE_RUST_LIBOBS}>>:cuda-load.c>
    $<$<BOOL:${ENABLE_RUST_LIBOBS}>:cuda-load-rust.c>
    nvenc-compat.c
    nvenc-cuda.c
    nvenc-helpers.c
    nvenc-helpers.h
    nvenc-internal.h
    nvenc-load.c
    nvenc-opts-parser.c
    nvenc-properties.c
    nvenc.c
    nvenc-override.c
    nvenc-override.h
    obs-nvenc.c
    obs-nvenc.h
)

target_link_libraries(
  obs-nvenc
  PRIVATE OBS::libobs OBS::opts-parser FFnvcodec::FFnvcodec $<$<PLATFORM_ID:Linux>:OBS::glad>
)

if(ENABLE_RUST_LIBOBS)
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
  target_link_libraries(obs-nvenc PRIVATE obs_nvenc_plugin-static)
  if(OS_LINUX)
    target_link_options(obs-nvenc PRIVATE "LINKER:--exclude-libs,ALL")
  endif()
endif()

target_compile_definitions(obs-nvenc PRIVATE $<$<BOOL:${ENABLE_NVENC_FFMPEG_IDS}>:REGISTER_FFMPEG_IDS>)

if(OS_WINDOWS)
  configure_file(cmake/windows/obs-module.rc.in obs-nvenc.rc)
  target_sources(obs-nvenc PRIVATE obs-nvenc.rc)
endif()

set_target_properties_obs(obs-nvenc PROPERTIES FOLDER plugins/obs-nvenc PREFIX "")
