# A replay saved after the filename settings changed must use them
# (obsproject/obs-studio#10923). Builds the plain C helper the frontend calls,
# so the test does not link the Qt frontend. Creating an output needs core
# video, so it runs (under xvfb-run) on Linux and skips without a display.
if(OS_LINUX AND TARGET OBS::libobs-opengl)
  add_executable(
    test_replay_buffer_naming
    test_replay_buffer_naming.c
    "${CMAKE_SOURCE_DIR}/frontend/utility/replay-buffer-naming.c"
    "${CMAKE_SOURCE_DIR}/frontend/utility/output-filename-format.c"
  )
  target_compile_definitions(test_replay_buffer_naming PRIVATE GRAPHICS_MODULE_PATH="$<TARGET_FILE:OBS::libobs-opengl>")
  target_include_directories(
    test_replay_buffer_naming
    PRIVATE ${CMOCKA_INCLUDE_DIR} "${CMAKE_SOURCE_DIR}/frontend/utility"
  )
  target_link_libraries(test_replay_buffer_naming PRIVATE OBS::libobs ${CMOCKA_LIBRARIES})
  add_dependencies(test_replay_buffer_naming libobs-opengl)
  add_dependencies(cmocka-tests test_replay_buffer_naming)
  # libobs finds its effect files relative to the working directory.
  add_test(
    NAME test_replay_buffer_naming
    COMMAND test_replay_buffer_naming
    WORKING_DIRECTORY "${OBS_OUTPUT_DIR}/$<CONFIG>"
  )
endif()
