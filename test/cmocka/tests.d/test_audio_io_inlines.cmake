# audio-io.h header-inline helpers
add_executable(test_audio_io_inlines test_audio_io_inlines.c)
target_include_directories(test_audio_io_inlines PRIVATE ${CMOCKA_INCLUDE_DIR})
target_link_libraries(test_audio_io_inlines PRIVATE OBS::libobs ${CMOCKA_LIBRARIES})

add_dependencies(cmocka-tests test_audio_io_inlines)
add_test(test_audio_io_inlines ${CMAKE_CURRENT_BINARY_DIR}/test_audio_io_inlines)
