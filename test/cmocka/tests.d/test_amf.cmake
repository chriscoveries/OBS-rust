# Decode AMF the way librtmp does for server messages.
if(TARGET OBS::outputs)
  add_executable(
    test_amf
    test_amf.c
    "${CMAKE_SOURCE_DIR}/plugins/obs-outputs/librtmp/amf.c"
    "${CMAKE_SOURCE_DIR}/plugins/obs-outputs/librtmp/log.c"
  )
  # No network code is built, so librtmp's TLS types are not needed.
  target_compile_definitions(test_amf PRIVATE NO_CRYPTO)
  target_include_directories(test_amf PRIVATE ${CMOCKA_INCLUDE_DIR} "${CMAKE_SOURCE_DIR}/plugins/obs-outputs")
  target_link_libraries(test_amf PRIVATE ${CMOCKA_LIBRARIES} $<$<PLATFORM_ID:Windows>:ws2_32>)
  add_dependencies(cmocka-tests test_amf)
  add_test(test_amf ${CMAKE_CURRENT_BINARY_DIR}/test_amf)
endif()
