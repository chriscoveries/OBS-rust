# Audio plane count for the mac-avcapture device path (helper is plain C).
if(TARGET OBS::libobs)
  add_executable(
    test_avcapture_audio_planes
    test_avcapture_audio_planes.c
    "${CMAKE_SOURCE_DIR}/plugins/mac-avcapture/avcapture-audio-planes.c"
  )
  target_include_directories(
    test_avcapture_audio_planes
    PRIVATE ${CMOCKA_INCLUDE_DIR} "${CMAKE_SOURCE_DIR}/plugins/mac-avcapture"
  )
  target_link_libraries(test_avcapture_audio_planes PRIVATE OBS::libobs ${CMOCKA_LIBRARIES})
  add_dependencies(cmocka-tests test_avcapture_audio_planes)
  add_test(test_avcapture_audio_planes ${CMAKE_CURRENT_BINARY_DIR}/test_avcapture_audio_planes)
endif()
