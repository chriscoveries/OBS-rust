if(NOT CORE_MODULE_SOURCE_IS_EXPLICIT_BINARY)
  message(
    FATAL_ERROR
    "libobs-core-modules must compile the generated binary-tree source '${EXPECTED_CORE_MODULE_SOURCE}'; configured sources: ${CORE_MODULE_SOURCES}"
  )
endif()
