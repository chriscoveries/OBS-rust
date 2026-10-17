# linux-v4l2: a camera another process holds open (EBUSY) names the holder,
# can be released with SIGTERM and is retried (obs-rust/obs-studio#799).
# Uses a fake /proc tree and real helper processes holding a plain file.
if(OS_LINUX AND TARGET OBS::v4l2)
  add_executable(test_v4l2_busy test_v4l2_busy.c "${CMAKE_SOURCE_DIR}/plugins/linux-v4l2/v4l2-busy.c.h")
  target_include_directories(test_v4l2_busy PRIVATE ${CMOCKA_INCLUDE_DIR} "${CMAKE_SOURCE_DIR}/plugins/linux-v4l2")
  target_link_libraries(test_v4l2_busy PRIVATE OBS::libobs ${CMOCKA_LIBRARIES})
  add_dependencies(cmocka-tests test_v4l2_busy)
  add_test(test_v4l2_busy ${CMAKE_CURRENT_BINARY_DIR}/test_v4l2_busy)
  set_tests_properties(test_v4l2_busy PROPERTIES TIMEOUT 60)
endif()
