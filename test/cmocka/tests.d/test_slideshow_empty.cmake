if(TARGET image-source AND TARGET obs-transitions)
  # Exercise the legacy slideshow callbacks with real libobs transitions, without
  # a video session or loading image/transition modules.
  add_executable(
    test_slideshow_empty
    test_slideshow_empty.c
    "${CMAKE_SOURCE_DIR}/plugins/image-source/obs-slideshow.c.h"
    "${CMAKE_SOURCE_DIR}/plugins/obs-transitions/transition-cut.c.h"
  )
  target_include_directories(test_slideshow_empty PRIVATE ${CMOCKA_INCLUDE_DIR})
  target_link_libraries(
    test_slideshow_empty
    PRIVATE OBS::libobs ${CMOCKA_LIBRARIES} $<$<PLATFORM_ID:Windows>:OBS::w32-pthreads>
  )
  add_dependencies(cmocka-tests test_slideshow_empty)

  # libobs startup initializes X11 hotkeys on Linux; an isolated virtual display
  # supplies that platform service without a desktop or GPU.
  if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    find_program(XVFB_RUN_EXECUTABLE xvfb-run)
  endif()
  if(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND XVFB_RUN_EXECUTABLE)
    add_test(NAME test_slideshow_empty COMMAND ${XVFB_RUN_EXECUTABLE} -a $<TARGET_FILE:test_slideshow_empty>)
  else()
    add_test(test_slideshow_empty ${CMAKE_CURRENT_BINARY_DIR}/test_slideshow_empty)
  endif()
  set_tests_properties(test_slideshow_empty PROPERTIES TIMEOUT 60)
endif()
