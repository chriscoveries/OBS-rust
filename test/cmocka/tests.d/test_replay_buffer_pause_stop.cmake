# Replay buffer stop while a recording on the same encoders is paused
# (obsproject/obs-studio#7249). Builds obs-ffmpeg-mux.c directly so the
# obs-ffmpeg module and its hardware encoder probes are not loaded. Needs core
# video, so it runs (under xvfb-run) on Linux and skips without a display.
if(OS_LINUX AND TARGET OBS::libobs-opengl)
  add_executable(
    test_replay_buffer_pause_stop
    test_replay_buffer_pause_stop.c
    "${CMAKE_SOURCE_DIR}/plugins/obs-ffmpeg/obs-ffmpeg-mux.c.h"
  )
  target_compile_definitions(
    test_replay_buffer_pause_stop
    PRIVATE GRAPHICS_MODULE_PATH="$<TARGET_FILE:OBS::libobs-opengl>"
  )
  target_include_directories(
    test_replay_buffer_pause_stop
    PRIVATE ${CMOCKA_INCLUDE_DIR} "${CMAKE_SOURCE_DIR}/plugins/obs-ffmpeg"
  )
  target_link_libraries(
    test_replay_buffer_pause_stop
    PRIVATE OBS::libobs FFmpeg::avformat FFmpeg::avutil ${CMOCKA_LIBRARIES}
  )
  add_dependencies(test_replay_buffer_pause_stop libobs-opengl)
  add_dependencies(cmocka-tests test_replay_buffer_pause_stop)
  # libobs finds its effect files relative to the working directory.
  add_test(
    NAME test_replay_buffer_pause_stop
    COMMAND test_replay_buffer_pause_stop
    WORKING_DIRECTORY "${OBS_OUTPUT_DIR}/$<CONFIG>"
  )
  set_tests_properties(test_replay_buffer_pause_stop PROPERTIES TIMEOUT 180)
endif()
