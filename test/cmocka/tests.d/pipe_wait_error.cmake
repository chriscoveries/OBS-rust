# Seed the status slot around the real waitpid to make a failed-wait status
# read deterministic. The SIGCHLD/ECHILD path uses a real Linux child.
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
  add_executable(test_pipe_wait_error test_pipe_wait_error.c "${CMAKE_SOURCE_DIR}/libobs/util/pipe-posix.c.h")
  target_link_libraries(test_pipe_wait_error PRIVATE OBS::libobs)
  target_link_options(test_pipe_wait_error PRIVATE "LINKER:--wrap=waitpid")
  # libobs.so also calls waitpid; with --wrap its reference resolves to this
  # executable's __wrap_waitpid, which must not be hidden (-fvisibility=hidden
  # failed the ASan link: "hidden symbol ... is referenced by DSO").
  set_target_properties(test_pipe_wait_error PROPERTIES C_VISIBILITY_PRESET default)
  add_dependencies(cmocka-tests test_pipe_wait_error)
  add_test(test_pipe_wait_error ${CMAKE_CURRENT_BINARY_DIR}/test_pipe_wait_error)
  set_tests_properties(test_pipe_wait_error PROPERTIES TIMEOUT 30)
endif()
