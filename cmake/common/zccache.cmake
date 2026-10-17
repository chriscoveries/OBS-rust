# OBS CMake zccache module
#
# Caches C/C++ compiles with soldr's embedded zccache by using its host
# compiler wrapper (zccache-soldr <compiler> <args>) as the compiler launcher.
# It caches sources built against the precompiled headers and unity batches
# without extra configuration (ccache needs sloppiness changes for GCC PCH).
#
# Applies only when nothing else picked a launcher: an explicit
# CMAKE_<LANG>_COMPILER_LAUNCHER or ENABLE_CCACHE wins.
#
# TODO(soldr#3829): soldr has no stable launcher command yet, so the wrapper
# is found below the soldr root by path; switch to `soldr cc --wrap` once it
# ships.

include_guard(GLOBAL)

# Opt-in for now: with soldr 0.9.34 a compile can still wait indefinitely on a
# busy shared daemon (zackees/soldr#3815), and there is no stable launcher
# command yet (zackees/soldr#3829).
option(ENABLE_ZCCACHE "Cache C/C++ compiles with soldr's zccache when soldr is installed" OFF)
set(OBS_ZCCACHE_LAUNCHER "" CACHE FILEPATH "zccache-soldr wrapper to use as the compiler launcher (found when empty)")

# _obs_find_zccache: the newest zccache-soldr wrapper soldr has materialized.
function(_obs_find_zccache out)
  set(_roots "$ENV{SOLDR_HOME}" "$ENV{HOME}/.soldr")
  set(_found "")
  foreach(_root IN LISTS _roots)
    if(_root AND IS_DIRECTORY "${_root}")
      file(GLOB _wrappers LIST_DIRECTORIES FALSE "${_root}/v*/shims/images/*/zccache-soldr")
      list(APPEND _found ${_wrappers})
    endif()
  endforeach()
  set(_best "")
  set(_best_time "")
  foreach(_wrapper IN LISTS _found)
    file(TIMESTAMP "${_wrapper}" _time "%Y%m%d%H%M%S")
    if(NOT _best OR _time STRGREATER _best_time)
      set(_best "${_wrapper}")
      set(_best_time "${_time}")
    endif()
  endforeach()
  set(${out} "${_best}" PARENT_SCOPE)
endfunction()

if(ENABLE_ZCCACHE AND NOT ENABLE_CCACHE AND NOT CMAKE_C_COMPILER_LAUNCHER AND NOT CMAKE_CXX_COMPILER_LAUNCHER)
  set(_zccache "${OBS_ZCCACHE_LAUNCHER}")
  if(NOT _zccache)
    _obs_find_zccache(_zccache)
  endif()
  if(_zccache AND EXISTS "${_zccache}")
    message(STATUS "Caching C/C++ compiles with zccache: ${_zccache}")
    set(CMAKE_C_COMPILER_LAUNCHER "${_zccache}")
    set(CMAKE_CXX_COMPILER_LAUNCHER "${_zccache}")
  else()
    message(DEBUG "zccache: no soldr zccache-soldr wrapper found; compiles are not cached")
  endif()
endif()
