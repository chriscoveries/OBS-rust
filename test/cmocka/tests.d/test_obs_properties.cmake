# obs-properties.c. Pins the current C behavior. The port must not edit this test.
add_executable(test_obs_properties test_obs_properties.c)
target_include_directories(test_obs_properties PRIVATE ${CMOCKA_INCLUDE_DIR})
target_link_libraries(test_obs_properties PRIVATE OBS::libobs ${CMOCKA_LIBRARIES})
add_dependencies(cmocka-tests test_obs_properties)
add_test(test_obs_properties ${CMAKE_CURRENT_BINARY_DIR}/test_obs_properties)
