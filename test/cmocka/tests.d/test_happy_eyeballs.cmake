# shared/happy-eyeballs (#880). Pins the connector over loopback so moving its
# platform code behind per-platform files (#847) is checked on every OS.
if(TARGET OBS::happy-eyeballs)
  add_executable(test_happy_eyeballs test_happy_eyeballs.c)
  target_include_directories(test_happy_eyeballs PRIVATE ${CMOCKA_INCLUDE_DIR})
  target_link_libraries(
    test_happy_eyeballs
    PRIVATE OBS::libobs OBS::happy-eyeballs ${CMOCKA_LIBRARIES} $<$<PLATFORM_ID:Windows>:ws2_32>
  )
  add_dependencies(cmocka-tests test_happy_eyeballs)
  add_test(test_happy_eyeballs ${CMAKE_CURRENT_BINARY_DIR}/test_happy_eyeballs)
endif()
