# The OSS input's FreeBSD /dev/sndstat parser (#864), compiled on Linux from
# plugins/oss-audio/oss-input-freebsd.c.
if(OS_LINUX)
  add_executable(test_oss_sndstat test_oss_sndstat.c "${CMAKE_SOURCE_DIR}/plugins/oss-audio/oss-input-freebsd.c.h")
  target_include_directories(test_oss_sndstat PRIVATE ${CMOCKA_INCLUDE_DIR} "${CMAKE_SOURCE_DIR}/plugins/oss-audio")
  target_link_libraries(test_oss_sndstat PRIVATE OBS::libobs ${CMOCKA_LIBRARIES})
  add_dependencies(cmocka-tests test_oss_sndstat)
  add_test(test_oss_sndstat ${CMAKE_CURRENT_BINARY_DIR}/test_oss_sndstat)
endif()
