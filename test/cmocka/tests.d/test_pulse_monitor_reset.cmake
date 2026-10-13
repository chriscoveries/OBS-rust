# Failed PulseAudio reset must preserve the live monitor's resource lifetime.
# Interpose external libpulse symbols in the executable, as test_pulse_default_id
# does; monitoring and the resampler are exercised in the production libobs.
if(OS_LINUX AND ENABLE_PULSEAUDIO)
  add_executable(test_pulse_monitor_reset test_pulse_monitor_reset.c)
  set_target_properties(test_pulse_monitor_reset PROPERTIES C_VISIBILITY_PRESET default ENABLE_EXPORTS ON)
  target_include_directories(test_pulse_monitor_reset PRIVATE ${CMOCKA_INCLUDE_DIR})
  target_link_libraries(test_pulse_monitor_reset PRIVATE OBS::libobs PulseAudio::PulseAudio ${CMOCKA_LIBRARIES})
  add_dependencies(cmocka-tests test_pulse_monitor_reset)
  add_test(
    test_pulse_monitor_reset
    ${CMAKE_CURRENT_BINARY_DIR}/test_pulse_monitor_reset
    failed_reset_preserves_monitor_lifetime
  )
  set_tests_properties(test_pulse_monitor_reset PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 30)
  foreach(
    _case
    failed_context_reset_recovers
    terminated_context_reset_recovers
    shared_failed_context_reset_recovers
    failed_context_replacement_failure_is_safe
  )
    add_test(test_pulse_monitor_reset_${_case} ${CMAKE_CURRENT_BINARY_DIR}/test_pulse_monitor_reset ${_case})
    set_tests_properties(test_pulse_monitor_reset_${_case} PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 30)
  endforeach()
endif()
