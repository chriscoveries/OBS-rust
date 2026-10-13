# Leaving an encoder group clears the encoder's group pointer, so a later
# obs_encoder_set_group() or the destroy path does not drop a reference the
# encoder no longer owns. obs_startup needs a display on Linux, so the case
# skips without one (run under xvfb-run).
add_executable(test_encoder_group_clear test_encoder_group_clear.c)
target_include_directories(test_encoder_group_clear PRIVATE ${CMOCKA_INCLUDE_DIR})
target_link_libraries(test_encoder_group_clear PRIVATE OBS::libobs ${CMOCKA_LIBRARIES})
add_dependencies(cmocka-tests test_encoder_group_clear)
add_test(test_encoder_group_clear ${CMAKE_CURRENT_BINARY_DIR}/test_encoder_group_clear)
