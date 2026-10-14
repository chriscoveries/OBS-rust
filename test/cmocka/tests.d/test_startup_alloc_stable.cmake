# bnum_allocs() must be stable once obs_startup() returns: background threads
# may not allocate lazily afterwards (obs-rust/obs-studio#663). obs_startup()
# needs a display on Linux, so the case skips without one.
add_executable(test_startup_alloc_stable test_startup_alloc_stable.c)
target_include_directories(test_startup_alloc_stable PRIVATE ${CMOCKA_INCLUDE_DIR})
target_link_libraries(test_startup_alloc_stable PRIVATE OBS::libobs ${CMOCKA_LIBRARIES})
add_dependencies(cmocka-tests test_startup_alloc_stable)
find_program(XVFB_RUN xvfb-run)
if(XVFB_RUN)
  add_test(test_startup_alloc_stable ${XVFB_RUN} -a ${CMAKE_CURRENT_BINARY_DIR}/test_startup_alloc_stable)
else()
  add_test(test_startup_alloc_stable ${CMAKE_CURRENT_BINARY_DIR}/test_startup_alloc_stable)
endif()
