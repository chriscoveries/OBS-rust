# libobs PulseAudio monitoring stream flags (obsproject/obs-studio#10342)
if((OS_LINUX OR OS_FREEBSD) AND ENABLE_PULSEAUDIO)
  find_package(PulseAudio REQUIRED)

  add_executable(
    test_pulse_monitor_flags
    test_pulse_monitor_flags.c
    "${CMAKE_SOURCE_DIR}/libobs/audio-monitoring/pulse/pulseaudio-stream-flags.c.h"
  )
  target_include_directories(
    test_pulse_monitor_flags
    PRIVATE ${CMOCKA_INCLUDE_DIR} "${CMAKE_SOURCE_DIR}/libobs/audio-monitoring/pulse"
  )
  target_link_libraries(test_pulse_monitor_flags PRIVATE PulseAudio::PulseAudio ${CMOCKA_LIBRARIES})
  add_dependencies(cmocka-tests test_pulse_monitor_flags)
  add_test(test_pulse_monitor_flags ${CMAKE_CURRENT_BINARY_DIR}/test_pulse_monitor_flags)
endif()
