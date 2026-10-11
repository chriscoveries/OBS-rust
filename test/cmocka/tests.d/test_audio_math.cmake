# audio-math.h header-inline helpers
add_executable(test_audio_math test_audio_math.c)
target_include_directories(test_audio_math PRIVATE ${CMOCKA_INCLUDE_DIR})
target_link_libraries(test_audio_math PRIVATE OBS::libobs ${CMOCKA_LIBRARIES})
if(NOT MSVC)
  target_link_libraries(test_audio_math PRIVATE m)
endif()

add_dependencies(cmocka-tests test_audio_math)
add_test(test_audio_math ${CMAKE_CURRENT_BINARY_DIR}/test_audio_math)
