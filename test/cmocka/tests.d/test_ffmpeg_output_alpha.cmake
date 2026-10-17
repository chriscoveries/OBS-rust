# A Custom FFmpeg Output recording WebM/VP9 from the BGRA colour format
# failed to open the encoder (obsproject/obs-studio#13785). Builds the real
# obs-ffmpeg-output.c into the test, so the obs-ffmpeg module and its
# hardware encoder probes are not loaded. Needs core video, so it runs
# (under xvfb-run) on Linux and skips without a display.
if(OS_LINUX AND TARGET OBS::libobs-opengl)
  add_executable(
    test_ffmpeg_output_alpha
    test_ffmpeg_output_alpha.c
    "${CMAKE_SOURCE_DIR}/plugins/obs-ffmpeg/obs-ffmpeg-output.c.h"
  )
  target_compile_definitions(test_ffmpeg_output_alpha PRIVATE GRAPHICS_MODULE_PATH="$<TARGET_FILE:OBS::libobs-opengl>")
  target_include_directories(
    test_ffmpeg_output_alpha
    PRIVATE ${CMOCKA_INCLUDE_DIR} "${CMAKE_SOURCE_DIR}/plugins/obs-ffmpeg"
  )
  target_link_libraries(
    test_ffmpeg_output_alpha
    PRIVATE
      OBS::libobs
      FFmpeg::avcodec
      FFmpeg::avformat
      FFmpeg::avutil
      FFmpeg::swscale
      FFmpeg::swresample
      ${CMOCKA_LIBRARIES}
  )
  add_dependencies(test_ffmpeg_output_alpha libobs-opengl)
  add_dependencies(cmocka-tests test_ffmpeg_output_alpha)
  # libobs finds its effect files relative to the working directory.
  add_test(
    NAME test_ffmpeg_output_alpha
    COMMAND test_ffmpeg_output_alpha
    WORKING_DIRECTORY "${OBS_OUTPUT_DIR}/$<CONFIG>"
  )
  set_tests_properties(test_ffmpeg_output_alpha PROPERTIES TIMEOUT 60)
endif()
