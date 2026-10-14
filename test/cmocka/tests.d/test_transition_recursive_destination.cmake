# Public transition graph conformance, bounded by a child-process watchdog. Each
# case forks a child with core video, so it runs (under xvfb-run) on Linux and
# skips without a display.
if(OS_LINUX AND TARGET OBS::libobs-opengl)
  add_executable(test_transition_recursive_destination test_transition_recursive_destination.c)
  target_compile_definitions(
    test_transition_recursive_destination
    PRIVATE GRAPHICS_MODULE_PATH="$<TARGET_FILE:OBS::libobs-opengl>"
  )
  target_include_directories(test_transition_recursive_destination PRIVATE ${CMOCKA_INCLUDE_DIR})
  target_link_libraries(test_transition_recursive_destination PRIVATE OBS::libobs ${CMOCKA_LIBRARIES})
  add_dependencies(test_transition_recursive_destination libobs-opengl)
  add_dependencies(cmocka-tests test_transition_recursive_destination)
  # libobs finds its effect files relative to the working directory.
  add_test(
    NAME test_transition_recursive_destination
    COMMAND test_transition_recursive_destination
    WORKING_DIRECTORY "${OBS_OUTPUT_DIR}/$<CONFIG>"
  )
  set_tests_properties(test_transition_recursive_destination PROPERTIES TIMEOUT 180)
endif()
