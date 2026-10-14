# FFmpeg media playback reads the source-owned options on its worker thread.
# Wrap FFmpeg's parser in this headless test to pause that read while the
# caller releases its settings string, wrap bfree to count releases of the
# option strings, and wrap pthread_join to hold the read until the worker is
# joined. No libobs test hook is needed.
# Under LeakSanitizer the full-decode cases also reach the AVPacket that
# mp_media_next_packet leaves behind on a read error or EOF (media.c), so this
# test only exits clean under sanitizers once that leak is fixed.
if(OS_LINUX AND TARGET OBS::media-playback)
  add_executable(test_media_source_options_lifetime test_media_source_options_lifetime.c)
  target_include_directories(test_media_source_options_lifetime PRIVATE ${CMOCKA_INCLUDE_DIR})
  target_link_libraries(
    test_media_source_options_lifetime
    PRIVATE OBS::libobs OBS::media-playback FFmpeg::swscale FFmpeg::swresample ${CMOCKA_LIBRARIES} m
  )
  target_link_options(
    test_media_source_options_lifetime
    PRIVATE "-Wl,--wrap=av_dict_parse_string" "-Wl,--wrap=bfree" "-Wl,--wrap=pthread_join"
  )
  add_dependencies(cmocka-tests test_media_source_options_lifetime)
  add_test(NAME test_media_source_options_lifetime COMMAND test_media_source_options_lifetime)
  set_tests_properties(test_media_source_options_lifetime PROPERTIES TIMEOUT 30)
endif()
