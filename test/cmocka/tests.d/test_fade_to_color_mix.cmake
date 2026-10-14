# Fade to Color audio gains at switch points of 0% and 100% (obs-rust/obs-studio#488).
# Header-only math from the obs-transitions plugin; needs no display.
add_executable(test_fade_to_color_mix test_fade_to_color_mix.c)
target_include_directories(
  test_fade_to_color_mix
  PRIVATE ${CMOCKA_INCLUDE_DIR} "${CMAKE_SOURCE_DIR}/plugins/obs-transitions"
)
target_link_libraries(test_fade_to_color_mix PRIVATE ${CMOCKA_LIBRARIES})
if(NOT MSVC)
  target_link_libraries(test_fade_to_color_mix PRIVATE m)
endif()
add_dependencies(cmocka-tests test_fade_to_color_mix)
add_test(test_fade_to_color_mix ${CMAKE_CURRENT_BINARY_DIR}/test_fade_to_color_mix)
