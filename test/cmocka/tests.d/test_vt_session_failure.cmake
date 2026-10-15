if(OS_MACOS)
  add_executable(test_vt_session_failure test_vt_session_failure.c)
  target_link_libraries(
    test_vt_session_failure
    PRIVATE
      OBS::libobs
      "-framework CoreFoundation"
      "-framework CoreMedia"
      "-framework CoreVideo"
      "-framework VideoToolbox"
  )
  target_compile_options(test_vt_session_failure PRIVATE -fblocks)
  target_compile_definitions(test_vt_session_failure PRIVATE $<$<BOOL:${ENABLE_HEVC}>:ENABLE_HEVC>)
  add_dependencies(cmocka-tests test_vt_session_failure)
  add_test(NAME test_vt_session_failure COMMAND test_vt_session_failure)
endif()
