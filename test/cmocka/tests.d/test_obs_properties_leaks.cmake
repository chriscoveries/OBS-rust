# obs-properties.c leaks (obs-rust/obs-studio#831), checked under LeakSanitizer.
add_executable(test_obs_properties_leaks test_obs_properties_leaks.c)
target_include_directories(test_obs_properties_leaks PRIVATE ${CMOCKA_INCLUDE_DIR})
target_link_libraries(test_obs_properties_leaks PRIVATE OBS::libobs ${CMOCKA_LIBRARIES})
obs_test_sanitize(test_obs_properties_leaks address)
add_dependencies(cmocka-tests test_obs_properties_leaks)
add_test(test_obs_properties_leaks ${CMAKE_CURRENT_BINARY_DIR}/test_obs_properties_leaks)
