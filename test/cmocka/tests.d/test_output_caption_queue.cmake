# Caption queue lifecycle: the stop path must drain caption_text under
# caption_mutex, and destroying a track must free queued nodes.
# Reads the caption queue through obs-internal.h, so it needs the private
# libobs and libcaption include dirs.
add_executable(test_output_caption_queue test_output_caption_queue.c)
target_include_directories(
  test_output_caption_queue
  PRIVATE ${CMOCKA_INCLUDE_DIR} "${CMAKE_SOURCE_DIR}/libobs" "${CMAKE_SOURCE_DIR}/deps/libcaption"
)
target_link_libraries(test_output_caption_queue PRIVATE OBS::libobs OBS::caption ${CMOCKA_LIBRARIES})

add_dependencies(cmocka-tests test_output_caption_queue)
add_test(test_output_caption_queue ${CMAKE_CURRENT_BINARY_DIR}/test_output_caption_queue)
