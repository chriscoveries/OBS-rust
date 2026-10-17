# plugins/text-freetype2 platform defaults (#863), built from the Linux/BSD
# implementation alone.
if(OS_LINUX OR OS_FREEBSD OR OS_OPENBSD)
  add_executable(
    test_text_freetype2_platform
    test_text_freetype2_platform.c
    "${CMAKE_SOURCE_DIR}/plugins/text-freetype2/text-freetype2-host-posix.c.h"
  )
  target_include_directories(
    test_text_freetype2_platform
    PRIVATE ${CMOCKA_INCLUDE_DIR} "${CMAKE_SOURCE_DIR}/plugins/text-freetype2"
  )
  target_link_libraries(test_text_freetype2_platform PRIVATE OBS::libobs ${CMOCKA_LIBRARIES})
  add_dependencies(cmocka-tests test_text_freetype2_platform)
  add_test(test_text_freetype2_platform ${CMAKE_CURRENT_BINARY_DIR}/test_text_freetype2_platform)
endif()
