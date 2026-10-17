# linux-v4l2: a camera another process holds open announces itself to the
# frontend (popup) once, Force Camera closes the holder, and the camera starts
# when the holder lets go (obs-rust/obs-studio#799). Real v4l2-input.c on a
# real libobs source, fake libv4l2, forked helper processes as holders.
if(OS_LINUX AND TARGET OBS::v4l2)
  find_package(Libv4l2 REQUIRED)

  add_executable(
    test_v4l2_busy_signal
    test_v4l2_busy_signal.c
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
    test_v4l2_busy_signal
    PRIVATE
      ${CMOCKA_INCLUDE_DIR}
      "${CMAKE_SOURCE_DIR}/plugins/linux-v4l2"
      "$<TARGET_PROPERTY:Libv4l2::Libv4l2,INTERFACE_INCLUDE_DIRECTORIES>"
  )
  target_link_libraries(
    test_v4l2_busy_signal
    PRIVATE OBS::libobs FFmpeg::avcodec FFmpeg::avformat FFmpeg::avutil ${CMOCKA_LIBRARIES}
  )
  add_dependencies(cmocka-tests test_v4l2_busy_signal)
  add_test(test_v4l2_busy_signal ${CMAKE_CURRENT_BINARY_DIR}/test_v4l2_busy_signal)
  set_tests_properties(test_v4l2_busy_signal PROPERTIES TIMEOUT 60)
endif()
