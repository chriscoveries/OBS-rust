# platform-cocoa.m. Off Apple the test is a no-op so the aggregate still
# runs. test_platform.c does not call this file.
add_executable(test_platform_cocoa test_platform_cocoa.c)
target_include_directories(test_platform_cocoa PRIVATE ${CMOCKA_INCLUDE_DIR})
target_link_libraries(test_platform_cocoa PRIVATE OBS::libobs ${CMOCKA_LIBRARIES})
if(OS_MACOS)
  target_link_libraries(
    test_platform_cocoa
    PRIVATE "$<LINK_LIBRARY:FRAMEWORK,CoreFoundation.framework>" "$<LINK_LIBRARY:FRAMEWORK,Foundation.framework>"
  )
endif()
add_dependencies(cmocka-tests test_platform_cocoa)
add_test(test_platform_cocoa ${CMAKE_CURRENT_BINARY_DIR}/test_platform_cocoa)
