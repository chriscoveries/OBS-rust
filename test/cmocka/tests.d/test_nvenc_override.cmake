# obs-nvenc lookahead and UHQ B-frame decisions, without the NVIDIA SDK.
add_executable(test_nvenc_override test_nvenc_override.c "${CMAKE_SOURCE_DIR}/plugins/obs-nvenc/nvenc-override.c.h")
target_include_directories(test_nvenc_override PRIVATE ${CMOCKA_INCLUDE_DIR} "${CMAKE_SOURCE_DIR}/plugins/obs-nvenc")
target_link_libraries(test_nvenc_override PRIVATE ${CMOCKA_LIBRARIES})

add_dependencies(cmocka-tests test_nvenc_override)
add_test(test_nvenc_override ${CMAKE_CURRENT_BINARY_DIR}/test_nvenc_override)
