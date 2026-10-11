# Network media sources must give up on a silent connection so they
# reconnect (upstream obsproject/obs-studio#12958, #11537). The end-to-end
# case runs its own silent RTSP camera on 127.0.0.1; no server is needed.
if(TARGET OBS::media-playback)
  add_executable(test_media_network_timeout test_media_network_timeout.c)
  target_include_directories(test_media_network_timeout PRIVATE ${CMOCKA_INCLUDE_DIR})
  target_link_libraries(
    test_media_network_timeout
    PRIVATE
      OBS::libobs
      OBS::media-playback
      FFmpeg::swscale
      FFmpeg::swresample
      ${CMOCKA_LIBRARIES}
      $<$<NOT:$<PLATFORM_ID:Windows>>:m>
  )
  add_dependencies(cmocka-tests test_media_network_timeout)
  add_test(test_media_network_timeout ${CMAKE_CURRENT_BINARY_DIR}/test_media_network_timeout)
  set_tests_properties(test_media_network_timeout PROPERTIES TIMEOUT 120)
endif()
