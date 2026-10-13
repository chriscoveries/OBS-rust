if(UNIX AND TARGET OBS::libobs)
  add_executable(test_pipe_create2_public_argv test_pipe_create2_public_argv.c)
  target_include_directories(test_pipe_create2_public_argv PRIVATE ${CMOCKA_INCLUDE_DIR})
  target_link_libraries(test_pipe_create2_public_argv PRIVATE OBS::libobs ${CMOCKA_LIBRARIES})

  add_dependencies(cmocka-tests test_pipe_create2_public_argv)
  add_test(NAME test_pipe_create2_public_argv COMMAND test_pipe_create2_public_argv)
  set_tests_properties(test_pipe_create2_public_argv PROPERTIES TIMEOUT 20)
endif()
