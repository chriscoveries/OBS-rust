# SCK stop-wait and free decision for the mac-capture destroy paths
# (helper is plain C, driven by real os_event objects).
add_executable(test_sck_stop_wait test_sck_stop_wait.c "${CMAKE_SOURCE_DIR}/plugins/mac-capture/mac-sck-stop.c")
target_include_directories(test_sck_stop_wait PRIVATE ${CMOCKA_INCLUDE_DIR} "${CMAKE_SOURCE_DIR}/plugins/mac-capture")
target_link_libraries(test_sck_stop_wait PRIVATE OBS::libobs ${CMOCKA_LIBRARIES})
add_dependencies(cmocka-tests test_sck_stop_wait)
add_test(test_sck_stop_wait ${CMAKE_CURRENT_BINARY_DIR}/test_sck_stop_wait)
