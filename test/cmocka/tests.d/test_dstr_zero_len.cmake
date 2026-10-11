# dstr.c compiled into the test with UBSan aborting on the first report
# (obs-rust/obs-studio#174). The local definitions take precedence over
# libobs's exports on ELF; MSVC has no UBSan, so it is not built there.
if(NOT MSVC)
  add_executable(test_dstr_zero_len test_dstr_zero_len.c "${CMAKE_SOURCE_DIR}/libobs/util/dstr.c")
  target_include_directories(test_dstr_zero_len PRIVATE ${CMOCKA_INCLUDE_DIR})
  target_compile_options(test_dstr_zero_len PRIVATE -fsanitize=undefined -fno-sanitize-recover=undefined)
  target_link_options(test_dstr_zero_len PRIVATE -fsanitize=undefined)
  target_link_libraries(test_dstr_zero_len PRIVATE OBS::libobs ${CMOCKA_LIBRARIES})
  add_dependencies(cmocka-tests test_dstr_zero_len)
  add_test(test_dstr_zero_len ${CMAKE_CURRENT_BINARY_DIR}/test_dstr_zero_len)
endif()
