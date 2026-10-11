# Characterizes the POSIX time/sleep/dir helpers in
# libobs/util/platform-nix-time-dir.c before their Rust port.
if(OS_LINUX OR OS_FREEBSD OR OS_OPENBSD OR OS_MACOS)
  add_executable(test_platform_time_dir test_platform_time_dir.c)
  target_include_directories(test_platform_time_dir PRIVATE ${CMOCKA_INCLUDE_DIR})
  target_link_libraries(test_platform_time_dir PRIVATE OBS::libobs ${CMOCKA_LIBRARIES})

  add_dependencies(cmocka-tests test_platform_time_dir)
  add_test(test_platform_time_dir ${CMAKE_CURRENT_BINARY_DIR}/test_platform_time_dir)
endif()
