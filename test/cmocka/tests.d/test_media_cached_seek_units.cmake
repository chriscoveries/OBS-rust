if(TARGET OBS::media-playback)
  add_executable(test_media_cached_seek_units test_media_cached_seek_units.c)
  target_include_directories(test_media_cached_seek_units PRIVATE ${CMOCKA_INCLUDE_DIR})
  target_link_libraries(
    test_media_cached_seek_units
    PRIVATE
      OBS::libobs
      OBS::media-playback
      FFmpeg::swscale
      FFmpeg::swresample
      ${CMOCKA_LIBRARIES}
  )
  add_dependencies(cmocka-tests test_media_cached_seek_units)
  add_test(
    NAME test_media_cached_seek_units
    COMMAND test_media_cached_seek_units "${CMAKE_CURRENT_SOURCE_DIR}/data/media_cached_seek_units.mkv"
    WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
  )
  set_tests_properties(test_media_cached_seek_units PROPERTIES TIMEOUT 20)
endif()
