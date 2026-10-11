# Enhanced Broadcasting override translation. The test builds the plain C
# file directly so it does not link the Qt frontend.
add_executable(
  test_multitrack_encoder_override
  test_multitrack_encoder_override.c
  "${CMAKE_SOURCE_DIR}/frontend/utility/multitrack-encoder-override.c"
)
target_include_directories(
  test_multitrack_encoder_override
  PRIVATE ${CMOCKA_INCLUDE_DIR} "${CMAKE_SOURCE_DIR}/frontend/utility"
)
target_link_libraries(test_multitrack_encoder_override PRIVATE ${CMOCKA_LIBRARIES})

add_dependencies(cmocka-tests test_multitrack_encoder_override)
add_test(test_multitrack_encoder_override ${CMAKE_CURRENT_BINARY_DIR}/test_multitrack_encoder_override)
