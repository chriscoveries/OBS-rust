# Wayland hotkeys held while OBS is focused (obsproject/obs-studio#10701).
# Runs the real libobs hotkey thread on an isolated core; no Wayland display
# is needed.
if(OS_LINUX OR OS_FREEBSD)
  add_executable(
    test_wayland_hotkeys
    test_wayland_hotkeys.c
    "${CMAKE_SOURCE_DIR}/libobs/obs-hotkey.c"
    "${CMAKE_SOURCE_DIR}/libobs/obs-nix-hotkey-state.c"
  )
  target_compile_definitions(test_wayland_hotkeys PRIVATE obs=hotkey_test_obs)
  target_include_directories(test_wayland_hotkeys PRIVATE ${CMOCKA_INCLUDE_DIR} "${CMAKE_SOURCE_DIR}/libobs")
  target_link_libraries(test_wayland_hotkeys PRIVATE OBS::libobs OBS::caption ${CMOCKA_LIBRARIES})
  add_dependencies(cmocka-tests test_wayland_hotkeys)
  add_test(test_wayland_hotkeys ${CMAKE_CURRENT_BINARY_DIR}/test_wayland_hotkeys)
endif()
