# dstr.c compiled into the test with UBSan aborting on the first report
# (obs-rust/obs-studio#174). The local definitions take precedence over
# libobs's exports on ELF; MSVC has no UBSan, so it is not built there.
if(NOT MSVC)
  add_executable(test_dstr_zero_len test_dstr_zero_len.c "${CMAKE_SOURCE_DIR}/libobs/util/dstr.c.h")
  target_include_directories(test_dstr_zero_len PRIVATE ${CMOCKA_INCLUDE_DIR})
  obs_test_sanitize(test_dstr_zero_len undefined)
  target_link_libraries(test_dstr_zero_len PRIVATE OBS::libobs ${CMOCKA_LIBRARIES})
  add_dependencies(cmocka-tests test_dstr_zero_len)
  add_test(test_dstr_zero_len ${CMAKE_CURRENT_BINARY_DIR}/test_dstr_zero_len)
endif()
