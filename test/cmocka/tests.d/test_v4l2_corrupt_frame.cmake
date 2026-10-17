# linux-v4l2 capture must survive a corrupt MJPEG/H.264 frame
# (obsproject/obs-studio#12521, #9013, #9434). Runs the real decoder and the
# real capture thread against a fake libv4l2 in the test, so it needs no
# camera, kernel module or display.
if(OS_LINUX AND TARGET OBS::v4l2)
  find_package(Libv4l2 REQUIRED)

  add_executable(
    test_v4l2_corrupt_frame
    test_v4l2_corrupt_frame.c
    "${CMAKE_SOURCE_DIR}/plugins/linux-v4l2/v4l2-controls.c.h"
    "${CMAKE_SOURCE_DIR}/plugins/linux-v4l2/v4l2-decoder.c.h"
    "${CMAKE_SOURCE_DIR}/plugins/linux-v4l2/v4l2-frame-action.c.h"
    "${CMAKE_SOURCE_DIR}/plugins/linux-v4l2/v4l2-framerate.c.h"
    "${CMAKE_SOURCE_DIR}/plugins/linux-v4l2/v4l2-helpers.c.h"
    "${CMAKE_SOURCE_DIR}/plugins/linux-v4l2/v4l2-input-caps.c.h"
    "${CMAKE_SOURCE_DIR}/plugins/linux-v4l2/v4l2-busy.c.h"
    "${CMAKE_SOURCE_DIR}/plugins/linux-v4l2/v4l2-input.c.h"
  )
  # libv4l2 is not linked: the test defines v4l2_open, v4l2_ioctl and friends.
  target_include_directories(
    test_v4l2_corrupt_frame
    PRIVATE
      ${CMOCKA_INCLUDE_DIR}
      "${CMAKE_SOURCE_DIR}/plugins/linux-v4l2"
      "$<TARGET_PROPERTY:Libv4l2::Libv4l2,INTERFACE_INCLUDE_DIRECTORIES>"
  )
  target_link_libraries(
    test_v4l2_corrupt_frame
    PRIVATE OBS::libobs FFmpeg::avcodec FFmpeg::avformat FFmpeg::avutil ${CMOCKA_LIBRARIES}
  )
  add_dependencies(cmocka-tests test_v4l2_corrupt_frame)
  add_test(test_v4l2_corrupt_frame ${CMAKE_CURRENT_BINARY_DIR}/test_v4l2_corrupt_frame)
  set_tests_properties(test_v4l2_corrupt_frame PROPERTIES TIMEOUT 60)
endif()
