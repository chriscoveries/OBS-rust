# graphics math alias safety for the functions left after part 1 of #370
add_executable(test_graphics_alias_rest test_graphics_alias_rest.c)
target_include_directories(test_graphics_alias_rest PRIVATE ${CMOCKA_INCLUDE_DIR})
target_link_libraries(test_graphics_alias_rest PRIVATE OBS::libobs ${CMOCKA_LIBRARIES})
if(NOT MSVC)
  target_link_libraries(test_graphics_alias_rest PRIVATE m)
endif()

add_dependencies(cmocka-tests test_graphics_alias_rest)
add_test(test_graphics_alias_rest ${CMAKE_CURRENT_BINARY_DIR}/test_graphics_alias_rest)
