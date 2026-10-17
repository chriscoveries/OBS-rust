# obs_shutdown() after a failed obs_startup() must not crash (#1032).
# Points DISPLAY at an unused display so obs_startup fails deterministically.
if(OS_LINUX)
  add_executable(test_shutdown_after_failed_startup test_shutdown_after_failed_startup.c)
  target_include_directories(test_shutdown_after_failed_startup PRIVATE ${CMOCKA_INCLUDE_DIR})
  target_link_libraries(test_shutdown_after_failed_startup PRIVATE OBS::libobs ${CMOCKA_LIBRARIES})
  add_dependencies(cmocka-tests test_shutdown_after_failed_startup)
  add_test(
    NAME test_shutdown_after_failed_startup
    COMMAND test_shutdown_after_failed_startup
    WORKING_DIRECTORY "${OBS_OUTPUT_DIR}/$<CONFIG>"
  )
  set_tests_properties(test_shutdown_after_failed_startup PROPERTIES TIMEOUT 60)
endif()
