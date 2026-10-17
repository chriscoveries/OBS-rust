cmake_minimum_required(VERSION 3.28...3.30)

add_library(ipc-util INTERFACE)
add_library(OBS::ipc-util ALIAS ipc-util)

target_sources(
  ipc-util
  INTERFACE
    ipc-util/pipe.h
    ipc-util/platform-pipe-compat.h
    ipc-util/platform-pipe.h
    "$<$<PLATFORM_ID:Windows>:${CMAKE_CURRENT_SOURCE_DIR}/ipc-util/pipe-windows.h>"
)

# Unity group (BUILD.md), listed plainly: groups.cmake doesn't read sources
# behind generator expressions.
if(OS_WINDOWS)
  target_sources(ipc-util INTERFACE build/ipc-util.windows+.h.c)
endif()

target_include_directories(ipc-util INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}")

set_target_properties(ipc-util PROPERTIES FOLDER deps)
