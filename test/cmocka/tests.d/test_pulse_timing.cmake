# linux-pulseaudio record stream timestamps (obsproject/obs-studio#12835)
if(TARGET OBS::pulseaudio)
  add_executable(test_pulse_timing test_pulse_timing.c "${CMAKE_SOURCE_DIR}/plugins/linux-pulseaudio/pulse-timing.c.h")
  target_include_directories(
    test_pulse_timing
    PRIVATE ${CMOCKA_INCLUDE_DIR} "${CMAKE_SOURCE_DIR}/plugins/linux-pulseaudio"
  )
  target_link_libraries(test_pulse_timing PRIVATE OBS::libobs ${CMOCKA_LIBRARIES})
  add_dependencies(cmocka-tests test_pulse_timing)
  add_test(test_pulse_timing ${CMAKE_CURRENT_BINARY_DIR}/test_pulse_timing)
endif()
