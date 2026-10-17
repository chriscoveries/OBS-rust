# Characterizes media-io/video-io.c before its Rust port.
add_executable(test_video_io test_video_io.c)
target_include_directories(test_video_io PRIVATE ${CMOCKA_INCLUDE_DIR})
target_link_libraries(test_video_io PRIVATE OBS::libobs ${CMOCKA_LIBRARIES})

# The six non-EXPORT functions are hidden (-fvisibility=hidden), so a test
# cannot link them out of libobs. Compiling video-io.c into this binary makes
# its definitions take precedence over libobs's. That pins the C file. It does
# not exercise a Rust swap of those symbols.
add_executable(test_video_io_hidden test_video_io_hidden.c "${CMAKE_SOURCE_DIR}/libobs/media-io/video-io.c.h")
target_include_directories(test_video_io_hidden PRIVATE ${CMOCKA_INCLUDE_DIR})
target_link_libraries(test_video_io_hidden PRIVATE OBS::libobs ${CMOCKA_LIBRARIES})

add_dependencies(cmocka-tests test_video_io test_video_io_hidden)
# obs_startup needs an X display; run under xvfb where available so the
# cases can never silently skip (they assert obs_up loudly either way).
find_program(XVFB_RUN xvfb-run)
if(XVFB_RUN)
  add_test(test_video_io ${XVFB_RUN} -a ${CMAKE_CURRENT_BINARY_DIR}/test_video_io)
  add_test(test_video_io_hidden ${XVFB_RUN} -a ${CMAKE_CURRENT_BINARY_DIR}/test_video_io_hidden)
else()
  add_test(test_video_io ${CMAKE_CURRENT_BINARY_DIR}/test_video_io)
  add_test(test_video_io_hidden ${CMAKE_CURRENT_BINARY_DIR}/test_video_io_hidden)
endif()
