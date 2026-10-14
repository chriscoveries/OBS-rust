# OBS CMake Windows cross-architecture helper build serialization

include_guard(GLOBAL)

# obs_serialize_x86_build(<target>): <target> builds a second architecture
# with `cmake --build build_x86 ...` (or build_x64 on ARM64) as a POST_BUILD
# step. Run in parallel, those nested builds regenerate the same build tree's
# ZERO_CHECK at once and MSBuild fails with MSB3374 ("being used by another
# process"). Registered targets are chained at the end of the configure so
# their nested builds run one at a time.
function(obs_serialize_x86_build target)
  set_property(GLOBAL APPEND PROPERTY _OBS_X86_BUILD_TARGETS ${target})
  get_property(deferred GLOBAL PROPERTY _OBS_X86_BUILD_DEFERRED)
  if(NOT deferred)
    set_property(GLOBAL PROPERTY _OBS_X86_BUILD_DEFERRED TRUE)
    cmake_language(DEFER DIRECTORY "${CMAKE_SOURCE_DIR}" CALL _obs_chain_x86_builds)
  endif()
endfunction()

function(_obs_chain_x86_builds)
  get_property(targets GLOBAL PROPERTY _OBS_X86_BUILD_TARGETS)
  set(previous "")
  foreach(target IN LISTS targets)
    if(previous)
      add_dependencies(${target} ${previous})
    endif()
    set(previous ${target})
  endforeach()
endfunction()
