# utf8_to_wchar's insize == 0 (NUL-terminated) path for 3-byte sequences.
# The old sentinel limit (unsigned char *)-1 is not a valid one-past-the-end
# pointer; at -O2 GCC folds the length checks away and the function answers 0
# for U+0800-U+FFFF. These assertions hold the -O0 behaviour in every
# configuration.
if(NOT MSVC)
  add_executable(test_utf8_insize0 test_utf8_insize0.c "${CMAKE_SOURCE_DIR}/libobs/util/utf8.c.h")
  target_include_directories(test_utf8_insize0 PRIVATE ${CMOCKA_INCLUDE_DIR} "${CMAKE_SOURCE_DIR}/libobs")
  target_link_libraries(test_utf8_insize0 PRIVATE OBS::libobs ${CMOCKA_LIBRARIES})
  obs_test_sanitize(test_utf8_insize0 address,undefined)
  add_dependencies(cmocka-tests test_utf8_insize0)
  add_test(test_utf8_insize0 ${CMAKE_CURRENT_BINARY_DIR}/test_utf8_insize0)
endif()
