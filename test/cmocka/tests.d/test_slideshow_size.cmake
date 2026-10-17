# An Image Slide Show with an aspect ratio or another non-WxH size must not
# get a size of 0x0 (obsproject/obs-studio#11696). Builds the plain C parsing
# the source uses; needs neither libobs core nor a display.
add_executable(test_slideshow_size test_slideshow_size.c "${CMAKE_SOURCE_DIR}/plugins/image-source/slideshow-size.c.h")
target_include_directories(test_slideshow_size PRIVATE ${CMOCKA_INCLUDE_DIR} "${CMAKE_SOURCE_DIR}/plugins/image-source")
target_link_libraries(test_slideshow_size PRIVATE ${CMOCKA_LIBRARIES})
add_dependencies(cmocka-tests test_slideshow_size)
add_test(test_slideshow_size ${CMAKE_CURRENT_BINARY_DIR}/test_slideshow_size)
