# obs-ffmpeg VAAPI device fallback for a stale vaapi_device (obsproject/obs-studio#13773)
if(OS_LINUX OR OS_FREEBSD OR OS_OPENBSD)
  add_executable(
    test_vaapi_device_select
    test_vaapi_device_select.c
    "${CMAKE_SOURCE_DIR}/plugins/obs-ffmpeg/vaapi-device-select.c.h"
  )
  target_include_directories(
    test_vaapi_device_select
    PRIVATE ${CMOCKA_INCLUDE_DIR} "${CMAKE_SOURCE_DIR}/plugins/obs-ffmpeg"
  )
  target_link_libraries(test_vaapi_device_select PRIVATE ${CMOCKA_LIBRARIES})
  add_dependencies(cmocka-tests test_vaapi_device_select)
  add_test(test_vaapi_device_select ${CMAKE_CURRENT_BINARY_DIR}/test_vaapi_device_select)
endif()
