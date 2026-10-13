# Exercise the plugin's real gain callbacks without a graphics context.
if(TARGET OBS::transition)
  add_executable(test_fade_to_color_audio test_fade_to_color_audio.c)
  target_include_directories(test_fade_to_color_audio PRIVATE ${CMOCKA_INCLUDE_DIR})
  target_link_libraries(test_fade_to_color_audio PRIVATE OBS::libobs ${CMOCKA_LIBRARIES})
  if(NOT MSVC)
    target_link_libraries(test_fade_to_color_audio PRIVATE m)
  endif()

  add_dependencies(cmocka-tests test_fade_to_color_audio)
  add_test(test_fade_to_color_audio ${CMAKE_CURRENT_BINARY_DIR}/test_fade_to_color_audio)
endif()
