cmake_minimum_required(VERSION 3.28)
project(obs_scaler_bench LANGUAGES C)
if(WIN32)
  message(FATAL_ERROR "This standalone harness currently targets macOS and Linux")
endif()
if(NOT OBS_SOURCE_ROOT OR NOT SCALER_BASE_SOURCE OR NOT SCALER_HEAD_SOURCE)
  message(FATAL_ERROR "Set OBS_SOURCE_ROOT, SCALER_BASE_SOURCE, and SCALER_HEAD_SOURCE")
endif()
find_package(PkgConfig REQUIRED)
pkg_check_modules(FFMPEG REQUIRED IMPORTED_TARGET libswscale libavutil)
add_executable(obs-scaler-bench bench.c "${SCALER_BASE_SOURCE}" "${SCALER_HEAD_SOURCE}")
target_include_directories(obs-scaler-bench PRIVATE "${OBS_SOURCE_ROOT}/libobs/media-io" "${OBS_SOURCE_ROOT}/libobs")
target_compile_features(obs-scaler-bench PRIVATE c_std_11)
target_compile_options(obs-scaler-bench PRIVATE -Wall -Wextra -Werror)
target_link_libraries(obs-scaler-bench PRIVATE PkgConfig::FFMPEG m)
set_source_files_properties(
  "${SCALER_BASE_SOURCE}"
  PROPERTIES
    COMPILE_DEFINITIONS
      "video_scaler_create=ab_before_video_scaler_create;video_scaler_destroy=ab_before_video_scaler_destroy;video_scaler_scale=ab_before_video_scaler_scale;sws_scale=ab_before_sws_scale"
)
set_source_files_properties(
  "${SCALER_HEAD_SOURCE}"
  PROPERTIES
    COMPILE_DEFINITIONS
      "video_scaler_create=ab_after_video_scaler_create;video_scaler_destroy=ab_after_video_scaler_destroy;video_scaler_scale=ab_after_video_scaler_scale;sws_scale=ab_after_sws_scale"
)
