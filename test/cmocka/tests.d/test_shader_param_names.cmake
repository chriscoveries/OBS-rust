# graphics/shader-param-names.c. Pins the current C behavior. The port must not edit this test.
add_executable(test_shader_param_names test_shader_param_names.c)
target_include_directories(test_shader_param_names PRIVATE ${CMOCKA_INCLUDE_DIR})
target_link_libraries(test_shader_param_names PRIVATE OBS::libobs ${CMOCKA_LIBRARIES})
add_dependencies(cmocka-tests test_shader_param_names)
add_test(test_shader_param_names ${CMAKE_CURRENT_BINARY_DIR}/test_shader_param_names)
