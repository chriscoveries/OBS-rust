# shared/happy-eyeballs: a failed name lookup reports its own error (#903).
if(TARGET OBS::happy-eyeballs)
  add_executable(test_happy_eyeballs_gai_error test_happy_eyeballs_gai_error.c)
  target_include_directories(test_happy_eyeballs_gai_error PRIVATE ${CMOCKA_INCLUDE_DIR})
  target_link_libraries(
    test_happy_eyeballs_gai_error
    PRIVATE OBS::libobs OBS::happy-eyeballs ${CMOCKA_LIBRARIES} $<$<PLATFORM_ID:Windows>:ws2_32>
  )
  add_dependencies(cmocka-tests test_happy_eyeballs_gai_error)
  add_test(test_happy_eyeballs_gai_error ${CMAKE_CURRENT_BINARY_DIR}/test_happy_eyeballs_gai_error)
endif()
