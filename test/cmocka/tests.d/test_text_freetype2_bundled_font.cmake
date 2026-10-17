# The bundled JetBrainsMono Nerd Font resolves through the Linux Fontconfig
# backend without being installed (obs-rust/obs-studio#1012).
if(OS_LINUX AND TARGET OBS::text-freetype2)
  add_executable(
    test_text_freetype2_bundled_font
    test_text_freetype2_bundled_font.c
    "${CMAKE_SOURCE_DIR}/plugins/text-freetype2/find-font-unix.c.h"
    "${CMAKE_SOURCE_DIR}/plugins/text-freetype2/text-freetype2-host-posix.c.h"
  )
  # No <dir> entries: the host's installed fonts stay invisible (#1076).
  file(
    WRITE "${CMAKE_CURRENT_BINARY_DIR}/ft2-isolated-fonts.conf"
    "<?xml version=\"1.0\"?>\n<fontconfig><cachedir>${CMAKE_CURRENT_BINARY_DIR}/ft2-isolated-fonts-cache</cachedir></fontconfig>\n"
  )
  target_compile_definitions(
    test_text_freetype2_bundled_font
    PRIVATE
      BUNDLED_FONT_DIR="${CMAKE_SOURCE_DIR}/plugins/text-freetype2/data/fonts"
      ISOLATED_FONTCONFIG="${CMAKE_CURRENT_BINARY_DIR}/ft2-isolated-fonts.conf"
  )
  target_include_directories(
    test_text_freetype2_bundled_font
    PRIVATE ${CMOCKA_INCLUDE_DIR} "${CMAKE_SOURCE_DIR}/plugins/text-freetype2"
  )
  target_link_libraries(
    test_text_freetype2_bundled_font
    PRIVATE OBS::libobs Freetype::Freetype Fontconfig::Fontconfig ${CMOCKA_LIBRARIES}
  )
  add_dependencies(cmocka-tests test_text_freetype2_bundled_font)
  add_test(test_text_freetype2_bundled_font ${CMAKE_CURRENT_BINARY_DIR}/test_text_freetype2_bundled_font)

  # A machine with the font installed system-wide (obs-rust/obs-studio#1076):
  # the same binary under a fontconfig whose system font directory holds a
  # copy of the face. The default face must still resolve to the bundled files.
  set(ft2_host_fonts "${CMAKE_CURRENT_BINARY_DIR}/ft2-host-fonts")
  file(CREATE_LINK "${CMAKE_SOURCE_DIR}/plugins/text-freetype2/data/fonts" "${ft2_host_fonts}" SYMBOLIC)
  file(
    WRITE "${CMAKE_CURRENT_BINARY_DIR}/ft2-host-fonts.conf"
    "<?xml version=\"1.0\"?>\n<fontconfig><dir>${ft2_host_fonts}</dir><cachedir>${CMAKE_CURRENT_BINARY_DIR}/ft2-host-fonts-cache</cachedir></fontconfig>\n"
  )
  add_test(
    NAME test_text_freetype2_bundled_font_host_font
    COMMAND ${CMAKE_CURRENT_BINARY_DIR}/test_text_freetype2_bundled_font
  )
  set_tests_properties(
    test_text_freetype2_bundled_font_host_font
    PROPERTIES ENVIRONMENT "FONTCONFIG_FILE=${CMAKE_CURRENT_BINARY_DIR}/ft2-host-fonts.conf"
  )
endif()
