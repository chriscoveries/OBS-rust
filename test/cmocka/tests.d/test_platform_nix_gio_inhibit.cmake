# Verify C and Rust use the GIO portal inhibitor on Linux and FreeBSD. The test
# executable interposes the GIO calls, so it needs no session bus or portal.
if(CMAKE_SYSTEM_NAME STREQUAL "Linux" OR CMAKE_SYSTEM_NAME STREQUAL "FreeBSD")
  if(TARGET gio::gio)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(_gio_inhibit_test_glib REQUIRED IMPORTED_TARGET glib-2.0 gobject-2.0)
    add_executable(test_platform_nix_gio_inhibit test_platform_nix_gio_inhibit.c)
    target_include_directories(test_platform_nix_gio_inhibit PRIVATE ${CMOCKA_INCLUDE_DIR})
    target_link_libraries(
      test_platform_nix_gio_inhibit PRIVATE OBS::libobs gio::gio PkgConfig::_gio_inhibit_test_glib ${CMOCKA_LIBRARIES}
    )
    set_target_properties(test_platform_nix_gio_inhibit PROPERTIES ENABLE_EXPORTS ON)
    add_dependencies(cmocka-tests test_platform_nix_gio_inhibit)
    add_test(test_platform_nix_gio_inhibit ${CMAKE_CURRENT_BINARY_DIR}/test_platform_nix_gio_inhibit)
  endif()
endif()
