# Regression test for obs-rust/obs-studio#684: cycling obs_startup/obs_shutdown
# without pause must not crash (hotkey ready event use-after-free on macOS).
# obs_startup() needs a display on Linux, so the case skips without one.
add_executable(test_startup_cycle_stress test_startup_cycle_stress.c)
target_include_directories(test_startup_cycle_stress PRIVATE ${CMOCKA_INCLUDE_DIR})
target_link_libraries(test_startup_cycle_stress PRIVATE OBS::libobs ${CMOCKA_LIBRARIES})
add_dependencies(cmocka-tests test_startup_cycle_stress)
find_program(XVFB_RUN xvfb-run)
if(XVFB_RUN)
  add_test(test_startup_cycle_stress ${XVFB_RUN} -a ${CMAKE_CURRENT_BINARY_DIR}/test_startup_cycle_stress)
else()
  add_test(test_startup_cycle_stress ${CMAKE_CURRENT_BINARY_DIR}/test_startup_cycle_stress)
endif()
