# Settings > Output > Advanced: the Rescale Output resolution box follows the
# filter box and the multitrack video lock. Builds the plain C helper the
# settings dialog calls, so the test needs neither Qt nor a display.
add_executable(test_output_rescale test_output_rescale.c "${CMAKE_SOURCE_DIR}/frontend/utility/output-rescale.c")
target_include_directories(test_output_rescale PRIVATE ${CMOCKA_INCLUDE_DIR} "${CMAKE_SOURCE_DIR}/frontend/utility")
target_link_libraries(test_output_rescale PRIVATE ${CMOCKA_LIBRARIES})
add_dependencies(cmocka-tests test_output_rescale)
add_test(test_output_rescale ${CMAKE_CURRENT_BINARY_DIR}/test_output_rescale)
