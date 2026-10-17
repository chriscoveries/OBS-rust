# plugins/vlc-video: a Stop stays in force when the next playlist item opens
# right after it (#1011). Header-only helper, no libVLC needed.
add_executable(test_vlc_drain_gate test_vlc_drain_gate.c)
target_include_directories(test_vlc_drain_gate PRIVATE ${CMOCKA_INCLUDE_DIR} "${CMAKE_SOURCE_DIR}/plugins/vlc-video")
target_link_libraries(test_vlc_drain_gate PRIVATE OBS::libobs ${CMOCKA_LIBRARIES})
add_dependencies(cmocka-tests test_vlc_drain_gate)
add_test(test_vlc_drain_gate ${CMAKE_CURRENT_BINARY_DIR}/test_vlc_drain_gate)
