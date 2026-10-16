# Audio from a device whose clock is off its nominal rate must not drop out
# every few minutes (obsproject/obs-studio#4600). Starts libobs core audio;
# like the other core tests it runs on Linux only.
if(OS_LINUX)
  add_executable(test_audio_clock_drift test_audio_clock_drift.c)
  target_include_directories(test_audio_clock_drift PRIVATE ${CMOCKA_INCLUDE_DIR})
  target_link_libraries(test_audio_clock_drift PRIVATE OBS::libobs OBS::caption m ${CMOCKA_LIBRARIES})
  add_dependencies(cmocka-tests test_audio_clock_drift)
  add_test(test_audio_clock_drift ${CMAKE_CURRENT_BINARY_DIR}/test_audio_clock_drift)
  set_tests_properties(test_audio_clock_drift PROPERTIES RUN_SERIAL TRUE TIMEOUT 300)
endif()
