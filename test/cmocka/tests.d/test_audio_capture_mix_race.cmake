if(OS_LINUX AND TARGET OBS::libobs-opengl)
  add_executable(test_audio_capture_mix_race test_audio_capture_mix_race.c)
  target_compile_definitions(
    test_audio_capture_mix_race
    PRIVATE GRAPHICS_MODULE_PATH="$<TARGET_FILE:OBS::libobs-opengl>"
  )
  target_include_directories(test_audio_capture_mix_race PRIVATE ${CMOCKA_INCLUDE_DIR})
  target_link_libraries(test_audio_capture_mix_race PRIVATE OBS::libobs ${CMOCKA_LIBRARIES})
  add_dependencies(test_audio_capture_mix_race libobs-opengl)
  add_dependencies(cmocka-tests test_audio_capture_mix_race)
  find_program(XVFB_RUN_EXECUTABLE xvfb-run)
  if(XVFB_RUN_EXECUTABLE)
    add_test(
      NAME test_audio_capture_mix_race
      COMMAND
        "${XVFB_RUN_EXECUTABLE}" -a -s "-screen 0 1280x1024x24 -noreset" "$<TARGET_FILE:test_audio_capture_mix_race>"
      WORKING_DIRECTORY "${OBS_OUTPUT_DIR}/$<CONFIG>"
    )
  else()
    add_test(
      NAME test_audio_capture_mix_race
      COMMAND test_audio_capture_mix_race
      WORKING_DIRECTORY "${OBS_OUTPUT_DIR}/$<CONFIG>"
    )
  endif()
  set_tests_properties(test_audio_capture_mix_race PROPERTIES TIMEOUT 60)
endif()
