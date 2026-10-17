# Encoder error stops outputs without deadlocking or breaking idle outputs
# (obsproject/obs-studio#9946). Builds obs-ffmpeg-mux.c directly for the real
# replay_buffer output. Needs core video, so it runs (under xvfb-run) on Linux
# and skips without a display.
if(OS_LINUX AND TARGET OBS::libobs-opengl)
  add_executable(
    test_encoder_error_stop
    test_encoder_error_stop.c
    "${CMAKE_SOURCE_DIR}/plugins/obs-ffmpeg/obs-ffmpeg-mux.c.h"
  )
  target_compile_definitions(test_encoder_error_stop PRIVATE GRAPHICS_MODULE_PATH="$<TARGET_FILE:OBS::libobs-opengl>")
  target_include_directories(
    test_encoder_error_stop
    PRIVATE ${CMOCKA_INCLUDE_DIR} "${CMAKE_SOURCE_DIR}/plugins/obs-ffmpeg"
  )
  target_link_libraries(
    test_encoder_error_stop
    PRIVATE OBS::libobs OBS::caption FFmpeg::avformat FFmpeg::avutil ${CMOCKA_LIBRARIES} ${CMAKE_DL_LIBS}
  )
  # Export the executable's pthread_cond_wait interposer so it observes the
  # per-mix GPU encoder teardown wait inside libobs without a production test hook.
  set_target_properties(test_encoder_error_stop PROPERTIES ENABLE_EXPORTS ON)
  add_dependencies(test_encoder_error_stop libobs-opengl)
  add_dependencies(cmocka-tests test_encoder_error_stop)
  # libobs finds its effect files relative to the working directory.
  add_test(NAME test_encoder_error_stop COMMAND test_encoder_error_stop WORKING_DIRECTORY "${OBS_OUTPUT_DIR}/$<CONFIG>")
  set_tests_properties(test_encoder_error_stop PROPERTIES TIMEOUT 240)
endif()
