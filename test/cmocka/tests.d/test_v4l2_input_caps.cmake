# linux-v4l2 device left open when an input has no capabilities (obsproject/obs-studio#5422)
if(TARGET OBS::v4l2)
  add_executable(
    test_v4l2_input_caps
    test_v4l2_input_caps.c
    "${CMAKE_SOURCE_DIR}/plugins/linux-v4l2/v4l2-input-caps.c.h"
  )
  target_include_directories(
    test_v4l2_input_caps
    PRIVATE ${CMOCKA_INCLUDE_DIR} "${CMAKE_SOURCE_DIR}/plugins/linux-v4l2"
  )
  target_link_libraries(test_v4l2_input_caps PRIVATE ${CMOCKA_LIBRARIES})
  add_dependencies(cmocka-tests test_v4l2_input_caps)
  add_test(test_v4l2_input_caps ${CMAKE_CURRENT_BINARY_DIR}/test_v4l2_input_caps)
endif()
