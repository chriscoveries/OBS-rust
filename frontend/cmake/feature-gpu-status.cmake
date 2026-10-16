include_guard(DIRECTORY)

# GPU acceleration status light (obs-rust/obs-studio#846). The classifier is
# rust/obs-gpu-status, linked into the frontend through the staticlib
# rust/obs-gpu-status-frontend (like obs-nvenc links obs-nvenc-plugin), so
# libobs' exported symbols do not change.
# Corrosion is fetched by libobs/cmake/rust.cmake.
if(ENABLE_RUST_LIBOBS)
  corrosion_import_crate(
    MANIFEST_PATH "${CMAKE_SOURCE_DIR}/Cargo.toml"
    CRATES obs-gpu-status-frontend
    CRATE_TYPES staticlib
    PROFILE release
  )
  target_link_libraries(obs-studio PRIVATE obs_gpu_status_frontend-static)
  target_sources(obs-studio PRIVATE utility/obs-gpu-status.h)
  target_enable_feature(obs-studio "GPU acceleration status light" OBS_GPU_STATUS)
endif()

target_sources(obs-studio PRIVATE utility/GpuStatus.cpp utility/GpuStatus.hpp)
