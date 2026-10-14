# obs_encoder_set_group(enc, NULL) must clear the encoder's group pointer
# (obs-rust/obs-studio#483). libobs needs an X display on Linux, so the test
# runs under xvfb-run when it is installed and skips without a display.
add_executable(test_encoder_group_null test_encoder_group_null.c)
target_include_directories(test_encoder_group_null PRIVATE ${CMOCKA_INCLUDE_DIR})
target_link_libraries(test_encoder_group_null PRIVATE OBS::libobs ${CMOCKA_LIBRARIES})
add_dependencies(cmocka-tests test_encoder_group_null)
find_program(XVFB_RUN xvfb-run)
if(OS_LINUX AND XVFB_RUN)
  add_test(NAME test_encoder_group_null COMMAND ${XVFB_RUN} -a $<TARGET_FILE:test_encoder_group_null>)
else()
  add_test(test_encoder_group_null ${CMAKE_CURRENT_BINARY_DIR}/test_encoder_group_null)
endif()
