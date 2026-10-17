if(ENABLE_CORE_MODULES)
  add_executable(test_core_module_manifest test_core_module_manifest.c)
  target_include_directories(test_core_module_manifest PRIVATE ${CMOCKA_INCLUDE_DIR})
  target_link_libraries(test_core_module_manifest PRIVATE OBS::libobs-core-modules ${CMOCKA_LIBRARIES})

  add_dependencies(cmocka-tests test_core_module_manifest)
  add_test(test_core_module_manifest ${CMAKE_CURRENT_BINARY_DIR}/test_core_module_manifest)

  get_target_property(_core_module_sources libobs-core-modules SOURCES)
  get_target_property(_core_module_binary_dir libobs-core-modules BINARY_DIR)
  set(_expected_core_module_source "${_core_module_binary_dir}/obs-core-modules.c")
  if(_expected_core_module_source IN_LIST _core_module_sources)
    set(_core_module_source_is_explicit_binary TRUE)
  else()
    set(_core_module_source_is_explicit_binary FALSE)
  endif()
  string(REPLACE ";" ", " _core_module_sources_text "${_core_module_sources}")

  add_test(
    NAME test_core_module_manifest_source
    COMMAND
      ${CMAKE_COMMAND} "-DCORE_MODULE_SOURCE_IS_EXPLICIT_BINARY=${_core_module_source_is_explicit_binary}"
      "-DCORE_MODULE_SOURCES=${_core_module_sources_text}"
      "-DEXPECTED_CORE_MODULE_SOURCE=${_expected_core_module_source}" -P
      "${CMAKE_CURRENT_SOURCE_DIR}/check_core_module_manifest_source.cmake"
  )
endif()
