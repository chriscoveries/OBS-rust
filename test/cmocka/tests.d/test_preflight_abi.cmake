if(OS_LINUX OR OS_MACOS)
  # Windows is excluded: linking a Rust static library there needs extra system
  # libraries that this test does not set up.
  add_executable(test_preflight_abi test_preflight_abi.c)
  target_include_directories(test_preflight_abi PRIVATE ${CMOCKA_INCLUDE_DIR} "${CMAKE_SOURCE_DIR}/frontend/preflight")
  target_link_libraries(
    test_preflight_abi
    PRIVATE "${OBS_PREFLIGHT_ARCHIVE}" ${CMOCKA_LIBRARIES} Threads::Threads ${CMAKE_DL_LIBS}
  )
  add_dependencies(test_preflight_abi obs-preflight-rust)
  add_dependencies(cmocka-tests test_preflight_abi)
  add_test(NAME test_preflight_abi COMMAND test_preflight_abi)
  set_tests_properties(test_preflight_abi PROPERTIES TIMEOUT 60)
endif()
