# media-io/format-conversion.c
add_executable(test_format_conversion test_format_conversion.c)
target_include_directories(test_format_conversion PRIVATE ${CMOCKA_INCLUDE_DIR})
target_link_libraries(test_format_conversion PRIVATE OBS::libobs ${CMOCKA_LIBRARIES})

add_dependencies(cmocka-tests test_format_conversion)
add_test(test_format_conversion ${CMAKE_CURRENT_BINARY_DIR}/test_format_conversion)
