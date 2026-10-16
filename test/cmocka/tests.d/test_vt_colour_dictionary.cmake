if(OS_MACOS)
  add_executable(test_vt_colour_dictionary test_vt_colour_dictionary.c)
  target_link_libraries(
    test_vt_colour_dictionary
    PRIVATE
      OBS::libobs
      "-framework CoreFoundation"
      "-framework CoreMedia"
      "-framework CoreVideo"
      "-framework VideoToolbox"
  )
  target_compile_options(test_vt_colour_dictionary PRIVATE -fblocks)
  target_compile_definitions(test_vt_colour_dictionary PRIVATE $<$<BOOL:${ENABLE_HEVC}>:ENABLE_HEVC>)
  add_dependencies(cmocka-tests test_vt_colour_dictionary)
  add_test(NAME test_vt_colour_dictionary COMMAND test_vt_colour_dictionary)
endif()
