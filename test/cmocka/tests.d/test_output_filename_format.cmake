# Filename format for recordings and replay buffers (upstream #13786). The
# test builds the plain C file directly so it does not link the Qt frontend.
add_executable(
  test_output_filename_format
  test_output_filename_format.c
  "${CMAKE_SOURCE_DIR}/frontend/utility/output-filename-format.c"
)
target_include_directories(
  test_output_filename_format
  PRIVATE ${CMOCKA_INCLUDE_DIR} "${CMAKE_SOURCE_DIR}/frontend/utility"
)
target_link_libraries(test_output_filename_format PRIVATE ${CMOCKA_LIBRARIES})

add_dependencies(cmocka-tests test_output_filename_format)
add_test(test_output_filename_format ${CMAKE_CURRENT_BINARY_DIR}/test_output_filename_format)
