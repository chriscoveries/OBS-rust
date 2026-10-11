# obsproject/obs-studio#12843: id-keyed output getters must not crash on a NULL id.
add_executable(test_output_null_id test_output_null_id.c)
target_include_directories(test_output_null_id PRIVATE ${CMOCKA_INCLUDE_DIR})
target_link_libraries(test_output_null_id PRIVATE OBS::libobs ${CMOCKA_LIBRARIES})

add_dependencies(cmocka-tests test_output_null_id)
add_test(test_output_null_id ${CMAKE_CURRENT_BINARY_DIR}/test_output_null_id)
