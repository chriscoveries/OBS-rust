# A V4L2 camera stays busy while OBS records (obsproject/obs-studio#12508):
# the recording's muxer must not inherit the camera's descriptor. Runs the
# real v4l2-input.c against a fake libv4l2 in the test and a real child
# process, so it needs no camera, kernel module or display.
if(OS_LINUX AND TARGET OBS::v4l2)
  find_package(Libv4l2 REQUIRED)

  add_executable(
    test_v4l2_fd_cloexec
    test_v4l2_fd_cloexec.c
    "${CMAKE_SOURCE_DIR}/plugins/linux-v4l2/v4l2-busy.c.h"
    "${CMAKE_SOURCE_DIR}/plugins/linux-v4l2/v4l2-controls.c.h"
    "${CMAKE_SOURCE_DIR}/plugins/linux-v4l2/v4l2-decoder.c.h"
    "${CMAKE_SOURCE_DIR}/plugins/linux-v4l2/v4l2-frame-action.c.h"
    "${CMAKE_SOURCE_DIR}/plugins/linux-v4l2/v4l2-framerate.c.h"
    "${CMAKE_SOURCE_DIR}/plugins/linux-v4l2/v4l2-helpers.c.h"
    "${CMAKE_SOURCE_DIR}/plugins/linux-v4l2/v4l2-input-caps.c.h"
    "${CMAKE_SOURCE_DIR}/plugins/linux-v4l2/v4l2-input.c.h"
  )
  # libv4l2 is not linked: the test defines v4l2_open, v4l2_ioctl and friends.
  target_include_directories(
    test_v4l2_fd_cloexec
    PRIVATE
      ${CMOCKA_INCLUDE_DIR}
      "${CMAKE_SOURCE_DIR}/plugins/linux-v4l2"
      "$<TARGET_PROPERTY:Libv4l2::Libv4l2,INTERFACE_INCLUDE_DIRECTORIES>"
  )
  target_link_libraries(
    test_v4l2_fd_cloexec
    PRIVATE OBS::libobs FFmpeg::avcodec FFmpeg::avformat FFmpeg::avutil ${CMOCKA_LIBRARIES}
  )
  add_dependencies(cmocka-tests test_v4l2_fd_cloexec)
  add_test(test_v4l2_fd_cloexec ${CMAKE_CURRENT_BINARY_DIR}/test_v4l2_fd_cloexec)
  set_tests_properties(test_v4l2_fd_cloexec PROPERTIES TIMEOUT 60)
endif()
