# FreeType 2 text glyph atlas packing and eviction, without FreeType or a
# graphics device (upstream obsproject/obs-studio#11819).
add_executable(test_glyph_atlas test_glyph_atlas.c "${CMAKE_SOURCE_DIR}/plugins/text-freetype2/glyph-atlas.c.h")
target_include_directories(test_glyph_atlas PRIVATE ${CMOCKA_INCLUDE_DIR} "${CMAKE_SOURCE_DIR}/plugins/text-freetype2")
target_link_libraries(test_glyph_atlas PRIVATE ${CMOCKA_LIBRARIES})

add_dependencies(cmocka-tests test_glyph_atlas)
add_test(test_glyph_atlas ${CMAKE_CURRENT_BINARY_DIR}/test_glyph_atlas)
