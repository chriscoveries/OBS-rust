# Like test_encoding_lag_rescale, use real core video under xvfb-run on Linux.
if(OS_LINUX AND TARGET OBS::libobs-opengl)
  add_executable(test_output_interleave_restart test_output_interleave_restart.c)
  target_compile_definitions(
    test_output_interleave_restart
    PRIVATE GRAPHICS_MODULE_PATH="$<TARGET_FILE:OBS::libobs-opengl>"
  )
  target_include_directories(test_output_interleave_restart PRIVATE ${CMOCKA_INCLUDE_DIR})
  target_link_libraries(test_output_interleave_restart PRIVATE OBS::libobs ${CMOCKA_LIBRARIES})
  add_dependencies(test_output_interleave_restart libobs-opengl)
  add_dependencies(cmocka-tests test_output_interleave_restart)
  add_test(
    NAME test_output_interleave_restart
    COMMAND test_output_interleave_restart
    WORKING_DIRECTORY "${OBS_OUTPUT_DIR}/$<CONFIG>"
  )
  set_tests_properties(test_output_interleave_restart PROPERTIES TIMEOUT 30 SKIP_RETURN_CODE 77)
endif()
