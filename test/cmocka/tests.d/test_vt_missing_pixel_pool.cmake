if(OS_MACOS)
  add_executable(test_vt_missing_pixel_pool test_vt_missing_pixel_pool.c)
  target_link_libraries(
    test_vt_missing_pixel_pool
    PRIVATE
      OBS::libobs
      "-framework CoreFoundation"
      "-framework CoreMedia"
      "-framework CoreVideo"
      "-framework VideoToolbox"
  )
  target_compile_options(test_vt_missing_pixel_pool PRIVATE -fblocks)
  target_compile_definitions(test_vt_missing_pixel_pool PRIVATE $<$<BOOL:${ENABLE_HEVC}>:ENABLE_HEVC>)
  add_dependencies(cmocka-tests test_vt_missing_pixel_pool)
  add_test(NAME test_vt_missing_pixel_pool COMMAND test_vt_missing_pixel_pool)
endif()
