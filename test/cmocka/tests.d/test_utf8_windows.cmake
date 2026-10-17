# The Windows branch of utf8.c, which test_utf8 skips. utf8.c is not exported
# by libobs, so it is compiled into the test, as for test_utf8.
if(MSVC)
  add_executable(test_utf8_windows test_utf8_windows.c "${CMAKE_SOURCE_DIR}/libobs/util/utf8.c.h")
  target_include_directories(test_utf8_windows PRIVATE ${CMOCKA_INCLUDE_DIR})
  target_link_libraries(test_utf8_windows PRIVATE OBS::libobs ${CMOCKA_LIBRARIES})

  add_dependencies(cmocka-tests test_utf8_windows)
  add_test(test_utf8_windows ${CMAKE_CURRENT_BINARY_DIR}/test_utf8_windows)
endif()
