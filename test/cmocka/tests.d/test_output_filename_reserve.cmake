# Two recordings started in the same second got the same file
# (obsproject/obs-studio#10750). Builds the plain C name selection directly so
# the test does not link the Qt frontend. On Linux it also builds
# obs-ffmpeg-mux.c to check that the ffmpeg_muxer output keeps a reserved
# file; that case needs core video, so it runs under xvfb-run and skips
# without a display.
add_executable(
  test_output_filename_reserve
  test_output_filename_reserve.c
  "${CMAKE_SOURCE_DIR}/frontend/utility/output-filename-reserve.c"
)
target_include_directories(
  test_output_filename_reserve
  PRIVATE ${CMOCKA_INCLUDE_DIR} "${CMAKE_SOURCE_DIR}/frontend/utility"
)
target_link_libraries(test_output_filename_reserve PRIVATE OBS::libobs ${CMOCKA_LIBRARIES})

if(OS_LINUX AND TARGET OBS::libobs-opengl)
  target_sources(test_output_filename_reserve PRIVATE "${CMAKE_SOURCE_DIR}/plugins/obs-ffmpeg/obs-ffmpeg-mux.c.h")
  target_compile_definitions(
    test_output_filename_reserve
    PRIVATE TEST_FFMPEG_MUX GRAPHICS_MODULE_PATH="$<TARGET_FILE:OBS::libobs-opengl>"
  )
  target_include_directories(test_output_filename_reserve PRIVATE "${CMAKE_SOURCE_DIR}/plugins/obs-ffmpeg")
  target_link_libraries(test_output_filename_reserve PRIVATE FFmpeg::avformat FFmpeg::avutil)
  add_dependencies(test_output_filename_reserve libobs-opengl)
endif()

add_dependencies(cmocka-tests test_output_filename_reserve)
if(OS_LINUX AND TARGET OBS::libobs-opengl)
  # libobs finds its effect files relative to the working directory.
  add_test(
    NAME test_output_filename_reserve
    COMMAND test_output_filename_reserve
    WORKING_DIRECTORY "${OBS_OUTPUT_DIR}/$<CONFIG>"
  )
else()
  add_test(NAME test_output_filename_reserve COMMAND test_output_filename_reserve)
endif()
set_tests_properties(test_output_filename_reserve PROPERTIES TIMEOUT 120)
