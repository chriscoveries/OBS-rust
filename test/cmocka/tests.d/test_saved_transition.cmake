# The current transition of a scene collection is found again after the UI
# language changed (built-in transitions are named in the UI language).
# Builds the plain C helper the frontend calls, so the test needs neither Qt
# nor a display.
add_executable(test_saved_transition test_saved_transition.c "${CMAKE_SOURCE_DIR}/frontend/utility/saved-transition.c")
target_include_directories(test_saved_transition PRIVATE ${CMOCKA_INCLUDE_DIR} "${CMAKE_SOURCE_DIR}/frontend/utility")
target_link_libraries(test_saved_transition PRIVATE ${CMOCKA_LIBRARIES})
add_dependencies(cmocka-tests test_saved_transition)
add_test(test_saved_transition ${CMAKE_CURRENT_BINARY_DIR}/test_saved_transition)
