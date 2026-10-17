# obs-hotkey-name-map.c. Pins the current C behavior. The port must not edit this test.
add_executable(test_hotkey_name_map test_hotkey_name_map.c)
target_include_directories(test_hotkey_name_map PRIVATE ${CMOCKA_INCLUDE_DIR})
target_link_libraries(test_hotkey_name_map PRIVATE OBS::libobs ${CMOCKA_LIBRARIES})
add_dependencies(cmocka-tests test_hotkey_name_map)
add_test(test_hotkey_name_map ${CMAKE_CURRENT_BINARY_DIR}/test_hotkey_name_map)
