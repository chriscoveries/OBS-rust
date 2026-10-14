# Generic output, encoder and service enumeration must keep the current
# context alive while the callback releases its owned reference, stop when the
# callback says so, drop its snapshot references afterwards, and skip encoders
# that are already being destroyed. The isolated child tests use POSIX fork.
if(NOT WIN32)
  add_executable(test_obs_enum_outputs_removal test_obs_enum_outputs_removal.c)
  target_include_directories(test_obs_enum_outputs_removal PRIVATE ${CMOCKA_INCLUDE_DIR})
  target_link_libraries(test_obs_enum_outputs_removal PRIVATE OBS::libobs ${CMOCKA_LIBRARIES})
  add_dependencies(cmocka-tests test_obs_enum_outputs_removal)
  add_test(test_obs_enum_outputs_removal ${CMAKE_CURRENT_BINARY_DIR}/test_obs_enum_outputs_removal)
  set_tests_properties(test_obs_enum_outputs_removal PROPERTIES TIMEOUT 90)
endif()
