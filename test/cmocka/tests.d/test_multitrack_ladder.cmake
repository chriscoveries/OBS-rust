# Encoding ladder preview rows. The test builds the plain C file directly
# so it does not link the Qt frontend.
add_executable(
  test_multitrack_ladder
  test_multitrack_ladder.c
  "${CMAKE_SOURCE_DIR}/frontend/utility/multitrack-ladder.c"
)
target_include_directories(test_multitrack_ladder PRIVATE ${CMOCKA_INCLUDE_DIR} "${CMAKE_SOURCE_DIR}/frontend/utility")
target_link_libraries(test_multitrack_ladder PRIVATE ${CMOCKA_LIBRARIES})

add_dependencies(cmocka-tests test_multitrack_ladder)
add_test(test_multitrack_ladder ${CMAKE_CURRENT_BINARY_DIR}/test_multitrack_ladder)
