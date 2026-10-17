# cross-<alias> targets (cross-win-x64, cross-mac-arm64, cross-mac-x64) and
# cross-all: run the cross-compile check in cmake/cross/ for another OS, in
# <build>/cross/<alias>. See cmake/cross/CMakeLists.txt and cmake/common/soldr.cmake.

include_guard(GLOBAL)

include(soldr)

add_custom_target(cross-all COMMENT "Cross-compile check: ${OBS_CROSS_ALIASES}")
foreach(alias IN LISTS OBS_CROSS_ALIASES)
  set(dir "${CMAKE_BINARY_DIR}/cross/${alias}")
  add_custom_target(
    cross-${alias}
    COMMAND
      "${CMAKE_COMMAND}" -S "${CMAKE_SOURCE_DIR}/cmake/cross" -B "${dir}" -G Ninja -DOBS_CROSS_TARGET=${alias}
      "$<$<BOOL:${OBS_SOLDR_EXECUTABLE}>:-DOBS_SOLDR_EXECUTABLE=${OBS_SOLDR_EXECUTABLE}>"
    COMMAND "${CMAKE_COMMAND}" --build "${dir}"
    COMMENT "Cross-compile check for ${alias}"
    USES_TERMINAL
    VERBATIM
  )
  add_dependencies(cross-all cross-${alias})
endforeach()
