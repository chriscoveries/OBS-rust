# Characterizes media-io/audio-io.c before its Rust port.
add_executable(test_audio_io test_audio_io.c)
target_include_directories(test_audio_io PRIVATE ${CMOCKA_INCLUDE_DIR})
target_link_libraries(test_audio_io PRIVATE OBS::libobs ${CMOCKA_LIBRARIES})

add_dependencies(cmocka-tests test_audio_io)
add_test(test_audio_io ${CMAKE_CURRENT_BINARY_DIR}/test_audio_io)
