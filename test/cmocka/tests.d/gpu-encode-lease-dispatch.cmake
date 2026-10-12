add_executable(
  test_gpu_encode_lease_dispatch
  test_gpu_encode_lease_dispatch.c
  "${CMAKE_SOURCE_DIR}/libobs/obs-video-gpu-encode.c"
  "${CMAKE_SOURCE_DIR}/libobs/obs-texture-lease.c"
)
target_include_directories(
  test_gpu_encode_lease_dispatch
  PRIVATE ${CMOCKA_INCLUDE_DIR} "${CMAKE_SOURCE_DIR}/libobs" "${CMAKE_SOURCE_DIR}/deps/libcaption"
)
target_link_libraries(test_gpu_encode_lease_dispatch PRIVATE OBS::libobs ${CMOCKA_LIBRARIES})
target_compile_definitions(test_gpu_encode_lease_dispatch PRIVATE OBS_TEST_GPU_ENCODE_THREAD)
add_dependencies(cmocka-tests test_gpu_encode_lease_dispatch)

if(APPLE)
  get_filename_component(FFmpeg_LIBRARY_DIR "${FFmpeg_avcodec_LIBRARY}" DIRECTORY)
  add_test(
    NAME test_gpu_encode_lease_dispatch
    COMMAND
      ${CMAKE_COMMAND} -E env "DYLD_FRAMEWORK_PATH=${CMAKE_BINARY_DIR}/libobs/$<CONFIG>"
      "DYLD_LIBRARY_PATH=${FFmpeg_LIBRARY_DIR}" "$<TARGET_FILE:test_gpu_encode_lease_dispatch>"
  )
else()
  add_test(NAME test_gpu_encode_lease_dispatch COMMAND "$<TARGET_FILE:test_gpu_encode_lease_dispatch>")
endif()
set_tests_properties(test_gpu_encode_lease_dispatch PROPERTIES TIMEOUT 30)
