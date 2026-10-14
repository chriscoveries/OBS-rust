# Verify that a queued restart or a deferred settings update does not leave a
# local media source PLAYING with no media after a close request raised by EOF,
# and that network sources keep their delayed reconnect. The test drives the
# source callbacks in libobs's video-tick order, without a GUI.
if(OS_LINUX AND TARGET OBS::media-playback)
  add_executable(test_ffmpeg_restart_close test_ffmpeg_restart_close.c)
  target_include_directories(
    test_ffmpeg_restart_close
    PRIVATE ${CMOCKA_INCLUDE_DIR} "${CMAKE_SOURCE_DIR}/plugins/obs-ffmpeg"
  )
  target_link_libraries(
    test_ffmpeg_restart_close
    PRIVATE
      OBS::libobs
      OBS::media-playback
      FFmpeg::avcodec
      FFmpeg::avformat
      FFmpeg::avutil
      FFmpeg::swscale
      FFmpeg::swresample
      ${CMOCKA_LIBRARIES}
      m
  )
  target_link_options(
    test_ffmpeg_restart_close
    PRIVATE
      "-Wl,--wrap=obs_module_text"
      "-Wl,--wrap=obs_source_get_name"
      "-Wl,--wrap=obs_source_showing"
      "-Wl,--wrap=obs_source_active"
      "-Wl,--wrap=obs_source_media_ended"
      "-Wl,--wrap=obs_source_media_started"
      "-Wl,--wrap=obs_source_output_video"
      "-Wl,--wrap=media_playback_create"
      "-Wl,--wrap=media_playback_play"
      "-Wl,--wrap=media_playback_has_video"
      "-Wl,--wrap=media_playback_destroy"
  )
  add_dependencies(cmocka-tests test_ffmpeg_restart_close)
  add_test(NAME test_ffmpeg_restart_close COMMAND test_ffmpeg_restart_close)
  set_tests_properties(test_ffmpeg_restart_close PROPERTIES TIMEOUT 30)
endif()
