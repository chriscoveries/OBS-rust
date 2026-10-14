if(UNIX AND TARGET OBS::libobs)
  add_executable(test_pipe_shell_argv0 test_pipe_shell_argv0.c)
  target_include_directories(test_pipe_shell_argv0 PRIVATE ${CMOCKA_INCLUDE_DIR})
  target_link_libraries(test_pipe_shell_argv0 PRIVATE OBS::libobs ${CMOCKA_LIBRARIES})

  add_dependencies(cmocka-tests test_pipe_shell_argv0)
  add_test(NAME test_pipe_shell_argv0 COMMAND test_pipe_shell_argv0)
  set_tests_properties(test_pipe_shell_argv0 PROPERTIES TIMEOUT 20)
endif()
