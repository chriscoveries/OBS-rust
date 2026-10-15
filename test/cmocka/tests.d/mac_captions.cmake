if(OS_MACOS)
  add_executable(
    test_mac_captions
    test_mac_captions.c
    test_mac_captions_stub.c
  )
  target_include_directories(
    test_mac_captions
    PRIVATE
      ${CMOCKA_INCLUDE_DIR}
      "${CMAKE_CURRENT_SOURCE_DIR}"
      "${CMAKE_SOURCE_DIR}/libobs"
      "${CMAKE_SOURCE_DIR}/deps/libcaption"
  )
  target_compile_definitions(
    test_mac_captions
    PRIVATE
      MAC_CAPTIONS_AUDIO_FIXTURE="${CMAKE_SOURCE_DIR}/test/cmocka/data/live-captions.wav"
      MAC_CAPTIONS_WORDS_FIXTURE="${CMAKE_SOURCE_DIR}/test/cmocka/data/live-captions.words"
  )
  target_link_libraries(test_mac_captions PRIVATE OBS::libobs OBS::caption ${CMOCKA_LIBRARIES})
  add_dependencies(cmocka-tests test_mac_captions)
  add_test(test_mac_captions ${CMAKE_CURRENT_BINARY_DIR}/test_mac_captions)
endif()
