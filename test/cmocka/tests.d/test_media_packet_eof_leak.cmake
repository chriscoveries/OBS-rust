if(TARGET OBS::libobs AND TARGET FFmpeg::avformat)
  find_package(FFmpeg 8.0 REQUIRED avdevice)
  add_executable(
    test_media_packet_eof_leak
    test_media_packet_eof_leak.c
    "${CMAKE_SOURCE_DIR}/shared/media-playback/media-playback/decode.c.h"
    "${CMAKE_SOURCE_DIR}/shared/media-playback/media-playback/network-timeout.c.h"
  )
  target_include_directories(
    test_media_packet_eof_leak
    PRIVATE
      ${CMOCKA_INCLUDE_DIR}
      "${CMAKE_SOURCE_DIR}/shared/media-playback"
      "${CMAKE_SOURCE_DIR}/shared/media-playback/media-playback"
  )
  target_link_libraries(
    test_media_packet_eof_leak
    PRIVATE
      OBS::libobs
      FFmpeg::avcodec
      FFmpeg::avdevice
      FFmpeg::avformat
      FFmpeg::avutil
      FFmpeg::swscale
      FFmpeg::swresample
      ${CMOCKA_LIBRARIES}
      $<$<NOT:$<PLATFORM_ID:Windows>>:m>
  )
  add_dependencies(cmocka-tests test_media_packet_eof_leak)
  add_test(
    NAME test_media_packet_eof_leak
    COMMAND test_media_packet_eof_leak "${CMAKE_CURRENT_SOURCE_DIR}/data/media_packet_eof_leak.mkv"
    WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
  )
  set_tests_properties(test_media_packet_eof_leak PROPERTIES TIMEOUT 20)
endif()
