# obs_scene_enum_items() must not walk a scene item the callback removed, and
# must not read through a released item (obs-rust/obs-studio#490). Each case
# forks a child with its own libobs and needs no video; obs_startup() needs a
# display on Linux, so the test runs under xvfb-run when it is installed and
# the cases skip without a display.
if(NOT WIN32)
  add_executable(test_scene_enum_items_removal test_scene_enum_items_removal.c)
  target_include_directories(test_scene_enum_items_removal PRIVATE ${CMOCKA_INCLUDE_DIR})
  target_link_libraries(test_scene_enum_items_removal PRIVATE OBS::libobs ${CMOCKA_LIBRARIES} Threads::Threads)
  add_dependencies(cmocka-tests test_scene_enum_items_removal)
  find_program(XVFB_RUN xvfb-run)
  if(OS_LINUX AND XVFB_RUN)
    add_test(NAME test_scene_enum_items_removal COMMAND ${XVFB_RUN} -a $<TARGET_FILE:test_scene_enum_items_removal>)
  else()
    add_test(test_scene_enum_items_removal ${CMAKE_CURRENT_BINARY_DIR}/test_scene_enum_items_removal)
  endif()
endif()
