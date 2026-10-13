# obs_scene_enum_items() must not walk a scene item the callback removed, and
# must not read through a released item. Each case forks a child with its own
# libobs and needs no video; obs_startup() needs a display on Linux, so there
# the cases skip without one.
if(NOT WIN32)
  add_executable(test_scene_enum_items_removal test_scene_enum_items_removal.c)
  target_include_directories(test_scene_enum_items_removal PRIVATE ${CMOCKA_INCLUDE_DIR})
  target_link_libraries(test_scene_enum_items_removal PRIVATE OBS::libobs ${CMOCKA_LIBRARIES} Threads::Threads)
  add_dependencies(cmocka-tests test_scene_enum_items_removal)
  add_test(test_scene_enum_items_removal ${CMAKE_CURRENT_BINARY_DIR}/test_scene_enum_items_removal)
endif()
